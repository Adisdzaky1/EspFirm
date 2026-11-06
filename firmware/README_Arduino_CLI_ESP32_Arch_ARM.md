Saya buatkan panduan pemulihan dari awal sampai akhir khusus untuk lingkungan Arch Linux ARM (aarch64) di Omulauncher, berdasarkan masalah yang tadi kita temukan.

Panduan ini bisa kamu simpan sebagai cadangan apabila Linux ter-reset, Arduino CLI harus dipasang ulang, atau toolchain ESP32 kembali mengalami kerusakan.

### Panduan instalasi dan perbaikan Arduino CLI ESP32

Konfigurasi yang digunakan

| Komponen    | Versi / konfigurasi |
| ----------- | ------------------- |
| OS          | Arch Linux ARM      |
| Arsitektur  | aarch64 / ARM64     |
| Arduino CLI | ARM64               |
| ESP32 Core  | 3.3.12              |
| Toolchain   | esp-x32 2601        |
| GCC         | 14.2.0              |
| Board       | ESP32 DevKit V1     |
| Partition   | huge_app            |
| Hasil akhir | .merged.bin         |

### Tahap 1 — Persiapan Linux

Perbarui paket Linux:

```bash
pacman -Syu
```

Instal kebutuhan dasar:

```bash
pacman -S --needed curl wget git tar gzip python
```

Periksa arsitektur:

```bash
uname -m
```

Hasil yang diharapkan:

```text
aarch64
```

### Tahap 2 — Instal Arduino CLI

Coba instal melalui repositori Arch Linux:

```bash
pacman -S --needed arduino-cli
```

Periksa instalasi:

```bash
arduino-cli version
```

Jika paket `arduino-cli` tidak tersedia di repositori yang digunakan, gunakan installer resmi Arduino CLI:

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Jika binary terpasang di folder `bin` lokal, tambahkan ke PATH sesuai lokasi instalasinya.

### Tahap 3 — Konfigurasi Arduino CLI

Buat konfigurasi:

```bash
arduino-cli config init
```

Perbarui indeks board:

```bash
arduino-cli core update-index
```

Instal ESP32 Core versi yang kita gunakan:

```bash
arduino-cli core install esp32:esp32@3.3.12
```

Periksa:

```bash
arduino-cli core list
```

Pastikan muncul:

```text
esp32:esp32  3.3.12
```

### Tahap 4 — Periksa arsip toolchain ESP32

Ini merupakan tahap penting untuk mencegah masalah sebelumnya.

Cari arsip toolchain:

```bash
find /root/.arduino15/staging/packages \
-type f -name '*xtensa-esp-elf*aarch64*.tar.gz'
```

Arsip yang kita gunakan sebelumnya:

```text
xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz
```

Tentukan lokasi arsip:

```bash
A="/root/.arduino15/staging/packages/xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz"
```

Pastikan file tersedia:

```bash
ls -lh "$A"
```

Jika file tidak ditemukan, periksa apakah instalasi ESP32 Core berhasil. Instalasi core seharusnya mengunduh toolchain yang dibutuhkan.

### Tahap 5 — Ekstrak toolchain

Buat folder pemulihan:

```bash
mkdir -p /tmp/xtensa_restore
```

Ekstrak arsip:

```bash
tar -xzf "$A" -C /tmp/xtensa_restore
```

Periksa compiler:

```bash
ls -lh /tmp/xtensa_restore/xtensa-esp-elf/bin/ | grep -E 'gcc|g\+\+'
```

Tes compiler asli:

```bash
/tmp/xtensa_restore/xtensa-esp-elf/bin/xtensa-esp-elf-gcc-14.2.0 --version
```

Hasil yang diharapkan:

```text
xtensa-esp-elf-gcc-14.2.0
(crosstool-NG esp-14.2.0_20260121) 14.2.0
```

### Tahap 6 — Memperbaiki broken symbolic link

Tahap terpenting

Masalah sebelumnya adalah symbolic link compiler mengarah ke folder sementara yang sudah hilang.

Contoh lokasi target yang rusak:

```text
/root/.arduino15/tmp/package-2725050860/
```

Nomor `2725050860` bisa berbeda setelah instalasi ulang. Karena itu, jangan menggunakan nomor tersebut secara permanen.

Gunakan perintah otomatis berikut untuk mendeteksi lokasi target berdasarkan symbolic link yang sebenarnya:

```bash
TOOL="/root/.arduino15/packages/esp32/tools/esp-x32/2601"

LINK=$(readlink "$TOOL/bin/xtensa-esp-elf-g++")

PKGROOT="${LINK%%/xtensa-esp-elf/*}"

echo "Target pemulihan: $PKGROOT"
```

Pastikan lokasi target berada di folder sementara Arduino:

```bash
case "$PKGROOT" in
  /root/.arduino15/tmp/package-*) echo "Lokasi target valid" ;;
  *) echo "Lokasi target tidak valid. Hentikan proses."; exit 1 ;;
esac
```

Salin kembali folder toolchain dari arsip:

```bash
mkdir -p "$PKGROOT"

cp -a /tmp/xtensa_restore/xtensa-esp-elf "$PKGROOT/"
```

Dengan cara ini, target symbolic link yang sebelumnya hilang akan dipulihkan.

### Tahap 7 — Tes compiler

Jalankan:

```bash
"$TOOL/bin/xtensa-esp-elf-gcc" --version
```

Lalu:

```bash
"$TOOL/bin/xtensa-esp-elf-g++" --version
```

Keduanya harus menampilkan versi:

```text
14.2.0
```

Periksa juga apakah symbolic link sudah aktif:

```bash
readlink -e "$TOOL/bin/xtensa-esp-elf-g++"
```

Jika perintah menghasilkan lokasi file, berarti link sudah berhasil ditemukan.

### Tahap 8 — Instal library sketch

Untuk sketch SmartDisplay MotoNav, library tambahan yang digunakan antara lain:

```bash
arduino-cli lib install "Adafruit SSD1306"
```

```bash
arduino-cli lib install "Adafruit GFX Library"
```

```bash
arduino-cli lib install "ArduinoJson"
```

```bash
arduino-cli lib install "UniversalTelegramBot"
```

Periksa library:

```bash
arduino-cli lib list
```

Catatan: `WiFi`, `BluetoothSerial`, `WebServer`, `HTTPClient`, `Preferences`, dan `SPI` sudah disediakan ESP32 Core.

### Tahap 9 — Compile ESP32

Pastikan sketch berada di:

```text
/root/ESP32_SmartDisplay_MotoNav_Combined
```

Jalankan:

```bash
arduino-cli compile \
--jobs 3 \
--fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
--build-path /root/arduino_build \
--export-binaries \
/root/ESP32_SmartDisplay_MotoNav_Combined
```

Jika berhasil, biasanya akan muncul pesan:

```text
Sketch uses ...
Global variables use ...
```

Diikuti pesan:

```text
Done compiling.
```

### Tahap 10 — Cari file firmware

Cari file `.merged.bin`:

```bash
find /root/arduino_build -type f -name "*.merged.bin"
```

Periksa ukuran file:

```bash
find /root/arduino_build -type f -name "*.merged.bin" -exec ls -lh {} \;
```

Jika file ditemukan, hasil firmware gabungan sudah tersedia.

### Bonus: Simpan perbaikan toolchain menjadi satu script

Supaya tidak perlu mengetik ulang Tahap 4 sampai Tahap 7, kamu bisa membuat script otomatis bernama `fix_xtensa.sh`.

Buat file:

```bash
nano fix_xtensa.sh
```

Masukkan kode berikut:

```bash

#!/bin/bash
set -e

echo "=================================="
echo " ESP32 XTENSA TOOLCHAIN REPAIR"
echo "=================================="

TOOL="/root/.arduino15/packages/esp32/tools/esp-x32/2601"
ARCHIVE="/root/.arduino15/staging/packages/xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz"

# Cek toolchain
if [ ! -d "$TOOL" ]; then
    echo "[ERROR] Folder ESP32 toolchain tidak ditemukan."
    exit 1
fi

# Cek arsip
if [ ! -f "$ARCHIVE" ]; then
    echo "[ERROR] Arsip toolchain tidak ditemukan."
    exit 1
fi

# Ambil target symbolic link
LINK=$(readlink "$TOOL/bin/xtensa-esp-elf-g++")
PKGROOT="${LINK%%/xtensa-esp-elf/*}"

case "$PKGROOT" in
    /root/.arduino15/tmp/package-*) ;;
    *)
        echo "[ERROR] Target symbolic link tidak valid."
        exit 1
        ;;
esac

echo "[INFO] Target: $PKGROOT"

# Ekstrak ulang arsip
mkdir -p /tmp/xtensa_restore
tar -xzf "$ARCHIVE" -C /tmp/xtensa_restore

# Pulihkan folder toolchain sementara
mkdir -p "$PKGROOT"
cp -a /tmp/xtensa_restore/xtensa-esp-elf "$PKGROOT/"

# Tes compiler
echo "[INFO] Testing GCC..."
"$TOOL/bin/xtensa-esp-elf-gcc" --version

echo "[INFO] Testing G++..."
"$TOOL/bin/xtensa-esp-elf-g++" --version

echo "=================================="
echo " TOOLCHAIN REPAIR SUCCESS"
echo "=================================="
```

Simpan dengan `CTRL + O`, Enter, lalu `CTRL + X`.

Beri izin eksekusi:

```bash
chmod +x fix_xtensa.sh
```

Jalankan kapan pun toolchain mengalami masalah:

```bash
./fix_xtensa.sh
```

Catatan penting untuk backup

Simpan juga arsip `xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz` di penyimpanan HP. Jika Linux ter-reset dan cache Arduino terhapus, kamu bisa memulihkan toolchain dari arsip tersebut setelah memasang ESP32 Core versi yang sesuai.

Script bonus ini mengasumsikan lokasi toolchain, versi core, dan nama arsip tetap sama seperti instalasi sekarang.

Saran saya: simpan `fix_xtensa.sh` dan arsip toolchain di folder Download Android. Dengan begitu, kalau lingkungan Omulauncher ter-reset, kamu tidak perlu mengulang proses diagnosis broken symbolic link dari nol.
