# ⚡ ESP32 Power Monitoring Firmware (Wi-Fi + Telegram)

Firmware ini dirancang untuk mikrokontroler **ESP32** yang berfungsi sebagai sistem pemantau daya listrik berbasis **Wi-Fi** dan terintegrasi dengan **Telegram Bot** sebagai media notifikasi otomatis.

---

## 📡 Fitur Utama

- 📊 **Pemantauan Daya Real-Time**  
  Membaca tegangan, arus, dan daya dari sensor seperti **PZEM-004T**.

- 🌐 **Koneksi Wi-Fi Otomatis**  
  Untuk mengirimkan data pemantauan ke Telegram atau dashboard.

- 🔔 **Notifikasi Telegram Otomatis**  
  Dikirim saat daya melewati batas atau terjadi gangguan.

- 🔌 **Kontrol Relay Otomatis/Manual**  
  Menyalakan atau mematikan beban listrik dari Telegram atau logika otomatis.

- 🔁 **Reboot dan Tes Fungsi Langsung**  
  Dukungan untuk restart perangkat dan uji kirim Telegram dari antarmuka serial.

---

## 🖼️ Perangkat Pendukung

### ESP32 DevKit
![ESP32 DevKit](pictures/esp32-devkitc-v4-esp32-wroom-32d-wireless-bluetooth-iot-wroom-esp-32d.jpg)

### Sensor PZEM-004T
![PZEM-004T](pictures/PZEM-004T-V2.0-700x700.jpg)

### Platform Pengembangan
![ESP-IDF Logo](pictures/logo-esp-idf.png)

---

## 📋 Konfigurasi yang Didukung

- SSID dan password Wi-Fi
- Token dan ID chat Telegram
- Batas maksimal daya (untuk alarm)
- ID sensor
- Mode kontrol relay (otomatis/manual)

---

## 🚀 Cara Penggunaan

1. **Flash firmware** ke board ESP32 menggunakan PlatformIO atau ESP-IDF.
2. Atur konfigurasi awal via **Serial** atau file konfigurasi `config.json`.
3. Sambungkan sensor daya dan relay.
4. Hubungkan ke Telegram untuk menerima notifikasi otomatis.

---

## 📁 Struktur Proyek

METERAN_ONLINE/<br />
├── .devcontainer/<br />
├── .vscode/<br />
├── build/<br />
├── main/<br />
│   ├── CMakeLists.txt<br />
│   ├── i2c-lcd.c<br />
│   ├── i2c-lcd.h<br />
│   ├── meteran_online.c<br />
│   ├── pzem004tv3.c<br />
│   ├── pzem004tv3.h<br />
│   └── telegram_root_cert.h<br />
├── pictures/<br />
├── CMakeLists.txt<br />
├── pytest_hello_world.py<br />
├── README.md<br />
├── sdkconfig<br />
├── sdkconfig.ci<br />
└── sdkconfig.old<br />

<br />

---

## 📄 Lisensi

Proyek ini berlisensi MIT. Silakan gunakan, modifikasi, dan kontribusikan sesuai kebutuhan.

© 2025 Fajar Labs

---


