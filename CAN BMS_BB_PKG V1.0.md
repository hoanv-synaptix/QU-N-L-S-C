CAN BMS\_BB\_PKG V1.0 

1\. Historical changes 

2\. Overview 

This agreement specifies the communication protocol between the BMS and other nodes in the  automotive CAN network. 

3\. Physical interface 

Physical interface This protocol adopts the CAN2.0A/B standard. The communication baud rate  is 250kbps. 

For multi-byte data during data transmission of this protocol, unless otherwise specified, the low byte will come first and the high byte will come last **(little endian).**

4\. Parameter group number 

| No  | Name  | describe  | Frame format  | ID  | Sender  | Message   cycle |
| :---: | :---- | ----- | :---: | :---: | ----- | ----- |
| 1  | BATT\_ST1  | Battery status   information 1 | Standard  frame | 0x02F4  | BMS  | 20ms |
| 2  | CELL\_VOLT  | Cell voltage  | Standard  frame | 0x04F4  | BMS  | 100ms |
| 3  | CELL\_TEMP  | Battery   temperature | Standard  frame | 0x05F4  | BMS  | 500ms |
| 4  | ALM\_INFO  | Alarm message  | Standard  frame | 0x07F4  | BMS  | 100ms |
| 5  | BATT\_ST2  | Battery status   information 2 | Extended  frames | 0x18F128F4  | BMS  | 100ms |
| 6  | Ctrl\_INFO  | Control   information | Extended  frames | 0x18F0F428  | Peripherals | Pending |
| 7  | ChgRequest\_INFO  | Battery info   request charger | Extended  frames | 0x18F0F472  | BMS  | 1000ms |
| 8 | BmsSwSta | Switch status | Extended  frames | 0x18F528F4 | BMS | 500ms |
| 9-17 | CELL\_VOLT\_FULL | Cell voltage full | Extended  frames | 0x18E028F4 \- 0x18E728F4 | BMS | 1000ms |
| 18 | CELL\_TEMP\_FULL | Cell temp full | Extended  frames | 0x18F228F4 | BMS | 1000ms |

5\. Message definition 

**5.1** （**BATT\_ST**）**ID**：**0x02F4**

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | ----- | ----- | :---- | :---- | ----- | :---- | :---- | ----- |
| 1  | BattVolt  | 0  | 16  | 0\~1000  | 0.1  | 0  | V  | Total battery   voltage |
| 2  | BattCurr  | 16  | 16  | \-400\~1000  | 0.1  | \-400  | A  | Total current of the  battery pack |
| 3  | SOC  | 32  | 8  | 0\~100  | 1  | 0  | %  | Remaining capacity |

 **5.2 (CELL\_VOLT**）**ID**：**0x04F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | ----- | ----- | :---- | ----- | ----- | :---- | :---- | :---- |
| 1  | MaxCellVolt  | 0  | 16  | 0\~5000  | 1  | 0  | mV  | Maximum single   unit voltage |
| 2  | MaxCvNO  | 16  | 8  | 1\~250  | 1  | 1  |  | The highest single  position |
| 3  | MinCellVolt  | 24  | 16  | 0\~5000  | 1  | 0  | mV  | Minimum single unit  voltage |
| 4  | MinCvNO  | 40  | 8  | 1\~250  | 1  | 1  |  | The lowest single  position |

**5.3 (CELL\_TEMP) ID: 0x05F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | ----- | ----- | :---- | :---- | ----- | :---- | :---- | :---- |
| 1  | MaxCellTemp  | 0  | 8  | \-50\~200  | 1  | \-50  | ℃  | Maximum battery  cell temperature |
| 2  | MaxCtNO  | 8  | 8  | 1\~125  | 1  | 1  |  | Maximum   temperature   position |
| 3  | MinCellTemp  | 16  | 8  | \-50\~200  | 1  | \-50  | ℃  | Minimum battery  cell temperature |
| 4  | MinCtNO  | 24  | 8  | 1\~125  | 1  | 1  |  | Minimum   temperature   position |
| 5  | AvrgCellTemp  | 32  | 8  | \-50\~200  | 1  | \-50  | ℃  | Average battery cell  temperature |

**5.4** （**ALM\_INFO**）**ID**：**0x07F4** 

The alarm information is sent by event triggering. When there is an alarm, the BMS periodically  sends the message, and if there is no alarm information, it will not be sent. When multiple alarms  occur simultaneously, the instrument 

The interface will display the alarm number cycle, and up to 4 alarm numbers can be displayed  cyclically. The alarm number is displayed in the order in which warnings occur. The specific format is  as follows:

| No | Parameter  | Start position  | bit length  | scope  | Resolution  | Offset  | unit  | Remark |
| :---- | :---- | ----- | ----- | ----- | ----- | :---- | :---- | :---- |
| 1 | Low pack volt | 0 | 2 | 0\~3 | 1 | 0 |  |  |
| 2 | Low cell volt | 2 | 2 | 0\~3 | 1 | 0 |  |  |
| 3 | High pack volt | 4 | 2 | 0\~3 | 1 | 0 |  |  |
| 4 | High cell volt | 6 | 2 | 0\~3 | 1 | 0 |  |  |
| 5 | Temp cell high charge | 8 | 2 | 0\~3 | 1 | 0 |  |  |
| 6 | Temp cell high discharge | 10 | 2 | 0\~3 | 1 | 0 |  |  |
| 7 | Temp cell low charge | 12 | 2 | 0\~3 | 1 | 0 |  |  |
| 8 | Temp cell low discharge | 14 | 2 | 0\~3 | 1 | 0 |  |  |
| 9 | Temp Relay high | 16 | 2 | 0\~3 | 1 | 0 |  |  |
| 10 | Over charge current  | 18 | 2 | 0\~3 | 1 | 0 |  |  |
| 11 | Over discharge current  | 20 | 2 | 0\~3 | 1 | 0 |  |  |
| 12 | Cell volt diff | 22 | 2 | 0\~3 | 1 | 0 |  |  |
| 13 | Low SOC | 24 | 2 | 0\~3 | 1 | 0 |  |  |
| 14 | reserve |  |  |  | 1 | 0 |  |  |
| 15 | reserve |  |  |  | 1 | 0 |  |  |
| 16 | reserve |  |  |  | 1 | 0 |  |  |

**5.5** （**BATT\_ST2**）**ID**：**0x18F128F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | ----- | ----- | ----- | :---: | ----- | ----- | ----- |
| 1  | Capacity Remain  | 0  | 16  | 0\~1000  | 0.1  | 0  | Ah  | Capacity Remain |
| 2  | Rate Capacity  | 16  | 16  | 0\~1000  | 0.1  | 0  | Ah  | Rate Capacity |
| 3  | Cycle count  | 32  | 16  | 0\~60000  | 1  | 0 |  |  |
| 4  | SOH  | 48  | 8  | 0\~100  | 1  | 0  | % |  |

**5.6** （**Ctrl\_INFO**）**ID**：**0x18F0F428  (Pending)**

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | ----- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | MaskCode  | 0  | 8  |  |  |  |  | 1: Allow control 0: Disable  control  bit0: Charging control  bit1: Discharge control |
| 2  | ChgSw  | 8  | 8  | 0\~1  |  |  |  | Charging switch, 0: Off 1:  On |
| 3  | DchgSw  | 16  | 8  | 0\~1  |  |  |  | DisCharging switch, 0: Off 1:  On |

**5.7**（**ChgRequest\_INFO**）**ID**：**0x18F0F472**

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | ----- | :---- | :---- | ----- | ----- | ----- | ----- | :---- |
| 1  | Batt Volt Request  | 0  | 16  | 0\~10000  | 0.1  | 0  | V  | Maximum requested  charging volt limit |
| 2  | Batt Curr Request  | 16  | 16  | 0\~10000  | 0.1  | 0  | A  | \= 95% Over current charge LV1 |

**5.8   (BmsSwSta）ID: 0x18F528F4**

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | Pre-discharge Status | 0  | 1 | 0\~1 |  | 0  |  | Relay/ Mosfet Pre-discharge status 0: open 1: closed |
| 2 | Discharge Status | 0  | 1 | 0\~1 |  | 0  |  | Relay/ Mosfet Discharge status 0: open 1: closed |
| 3 | Charge Status | 0  | 1 | 0\~1 |  | 0  |  | Relay/ Mosfet Charge status 0: open 1: closed |

**5.9-17   (CELL\_VOLT\_FULL）ID: 0x18E028F4 \- 0x18E728F4**  
**CAN ID : 0x18E028F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 1 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 2 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 3 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 4 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

0x18E028F4  AD 0E AB 0E A3 0E A6 0E  
AD 0E : cell 1     3757mV  
AB 0E : cell 2     3755mV  
A3 0E : cell 3     3747mV  
A6 0E : cell 4     3750mV

**CAN ID : 0x18E128F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 5 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 6 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 7 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 8 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**CAN ID : 0x18E228F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 9 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 10 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 11 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 12 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**CAN ID : 0x18E328F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 13 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 14 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 15 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 16 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**CAN ID : 0x18E428F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 17 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 18 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 19 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 20 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**CAN ID : 0x18E528F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 21 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 22 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 23 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 24 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**CAN ID : 0x18E628F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 25 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 26 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 27 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 28 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**CAN ID : 0x18E728F4** 

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1  | CELL 29 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 2 | CELL 30 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 3 | CELL 31 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |
| 4 | CELL 32 | 0 | 16 | 0\~10000 | 1 | 0 | mV |  |

**5.18   (CELL\_TEMP\_FULL）ID: 0x18F228F4**

| No  | Parameter  | Start   position | bit   length | scope  | Resolution  | Offset  | unit  | Remark |
| ----- | :---- | :---- | :---- | :---- | ----- | ----- | ----- | :---- |
| 1 | Temp Relay | 0 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Temp Shunt | 8 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Cell Temp1 | 16 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Cell Temp2 | 24 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Cell Temp3 | 32 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Cell Temp4 | 40 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Cell Temp5 | 48 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |
| 1 | Cell Temp6 | 56 | 8 | \-50\~200 | 1 | \-50 | ℃  |  |

