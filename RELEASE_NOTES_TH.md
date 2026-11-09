# Survival Wiki ภาษาไทย 1.7.0-th1

เพิ่มเมนู **GPS** บนหน้าหลักและใน **ตั้งค่า** สำหรับ ESP32-2432S028R / CYD 2.8 นิ้ว

- **หลงทาง** ดูพิกัด เปิดคู่มือภาษาไทย ตั้งจุดเริ่ม `START` ในชุดแผนที่ที่กำลังใช้ และส่งพิกัดพร้อมข้อความ `SOS` ให้บอร์ดใกล้เคียงผ่านเมช ESP-NOW
- **ไปยังจุดปัก** ดูลูกศรและระยะเส้นตรงไปยังจุดที่เลือก รวมถึง `START` หรือเลือกจุดจาก **แผนที่ → จุด → นำทาง**
- **เวลาและดวงอาทิตย์** อ่านเวลาจากโมดูล GPS ผ่าน NMEA RMC ค่าเริ่มต้น UTC+7 แบบ 24 ชั่วโมง แสดงเวลาอาทิตย์ขึ้นและตกโดยประมาณ ตั้งเตือนล่วงหน้าได้ 30, 60, 90 หรือ 120 นาที
- **สถิติเดินทาง** ดูความเร็ว ความสูง ระยะทางสะสม เวลาเคลื่อนที่ ความเร็วสูงสุด และความสูงที่ไต่ขึ้น สถิติเก็บใน RAM และเริ่มใหม่เมื่อกดล้างสถิติหรือรีสตาร์ต

**อัปเดตจาก `1.6.0-th4`** สำรองข้อมูลแล้วแฟลช `firmware-CYD28.bin` ที่ **`0x0`** ใช้ SD Card เดิมได้ ทั้งแผนที่หลายจังหวัด จุดปัก ประวัติทางเดิน และข้อมูลคู่มือ [วิธีแฟลช](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/blob/main/README_TH.md) · [คู่มือ GPS](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/blob/main/GPS_TH.md)

**ติดตั้งใหม่หรือเพิ่มพื้นที่แผนที่** ใช้ไฟล์จากรุ่น `1.6.0-th4` ร่วมกับเฟิร์มแวร์นี้ได้

- ชุด SD ภาษาไทยพร้อมแผนที่สุราษฎร์ธานี [sdcard-TH.zip](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/releases/download/v1.6.0-th4/sdcard-TH.zip)
- แผนที่สุราษฎร์ธานีแยกสำหรับการ์ดที่มีชุดภาษาไทยแล้ว [maps-Surat-Thani-offline.zip](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/releases/download/v1.6.0-th4/maps-Surat-Thani-offline.zip)
- ตัวช่วย Windows สำหรับเตรียมแผนที่ทั้งจังหวัด [Thailand-Offline-Province-Maps.zip](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/releases/download/v1.6.0-th4/Thailand-Offline-Province-Maps.zip)
- ตัวช่วย Windows สำหรับเลือกพื้นที่ขนาดเล็ก [Thailand-Map-Downloader.zip](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/releases/download/v1.6.0-th4/Thailand-Map-Downloader.zip)

ตรวจความถูกต้องของเฟิร์มแวร์ด้วย `SHA256SUMS.txt` ที่แนบมากับรุ่นนี้ สำหรับไฟล์ ZIP ให้ใช้ค่า SHA256 ของรุ่นที่ดาวน์โหลด [ชุดดาวน์โหลด 1.6.0-th4](https://github.com/zmikiz1423/esp32-cyd-survival-wiki-thai/releases/tag/v1.6.0-th4)

ขอขอบคุณ Everyday3D / BoxerDawg 3D และผู้ร่วมพัฒนาโครงการต้นฉบับ นำเฉพาะฟังก์ชัน GPS ที่ระบุจากรุ่น 1.7.0 มาปรับให้ทำงานกับฉบับภาษาไทยและแผนที่หลายจังหวัด โดยคง MIT License และเครดิตต้นฉบับไว้ [ซอร์สต้นฉบับ](https://github.com/everyday3d/esp32-cyd-survival-wiki/commit/99ea3bd0c10012965e30d16beda86343e0c8c319)

รุ่นนี้ผ่านการคอมไพล์และการตรวจซอฟต์แวร์ การใช้งานเมนูใหม่และโมดูล GPS บนบอร์ดจริงยังรอทดสอบ
