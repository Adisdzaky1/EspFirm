# Arduino CLI + ESP32 Toolchain di Arch Linux ARM

Panduan instalasi, pemulihan toolchain, kompilasi, dan pencarian
firmware `.merged.bin` untuk **Arch Linux ARM (aarch64)** di
Omulauncher/proot.

> Panduan ini disusun berdasarkan lingkungan yang berhasil diuji:
>
> -   OS: Arch Linux ARM
> -   Arsitektur: `aarch64`
> -   ESP32 Arduino Core: `3.3.12`
> -   Toolchain: `esp-x32/2601`
> -   GCC/G++: `14.2.0`
> -   Board: ESP32 DevKit V1 (`esp32:esp32:esp32`)
> -   Partition: `huge_app`

**Penting:** nama folder, nomor paket sementara, dan versi toolchain
dapat berubah pada instalasi berikutnya. Jangan menganggap ID folder
sementara selalu sama.

------------------------------------------------------------------------

## Daftar isi

1.  [Persiapan Linux](#1-persiapan-linux)
2.  [Instal Arduino CLI](#2-instal-arduino-cli)
3.  [Konfigurasi Arduino CLI](#3-konfigurasi-arduino-cli)
4.  [Instal ESP32 Core](#4-instal-esp32-core)
5.  [Periksa dan siapkan arsip
    toolchain](#5-periksa-dan-siapkan-arsip-toolchain)
6.  [Pulihkan broken symbolic link](#6-pulihkan-broken-symbolic-link)
7.  [Periksa library sketch](#7-periksa-library-sketch)
8.  [Kompilasi sketch ESP32](#8-kompilasi-sketch-esp32)
9.  [Cari file merged.bin](#9-cari-file-mergedbin)
10. [Script pemulihan otomatis](#10-script-pemulihan-otomatis)
11. [Troubleshooting](#11-troubleshooting)

------------------------------------------------------------------------

## 1. Persiapan Linux

Periksa arsitektur:

``` bash
uname -m
```

Hasil yang diharapkan:

``` text
aarch64
```

Perbarui sistem:

``` bash
pacman -Syu
```

Pasang kebutuhan dasar:

``` bash
pacman -S --needed curl wget git tar gzip python
```

## 2. Instal Arduino CLI

Coba instal dari repositori yang tersedia:

``` bash
pacman -S --needed arduino-cli
```

Periksa:

``` bash
arduino-cli version
```

Jika paket `arduino-cli` tidak tersedia di repositori, gunakan installer
resmi:

``` bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Setelah instalasi, pastikan binary `arduino-cli` dapat dipanggil:

``` bash
arduino-cli version
```

Jika perintah tidak ditemukan, periksa lokasi binary hasil instalasi dan
tambahkan foldernya ke `PATH`.

## 3. Konfigurasi Arduino CLI

Buat konfigurasi awal:

``` bash
arduino-cli config init
```

Perbarui indeks paket:

``` bash
arduino-cli core update-index
```

Jika `core update-index` gagal karena DNS atau koneksi, perbaiki koneksi
jaringan/DNS Linux terlebih dahulu, lalu ulangi perintah tersebut.

## 4. Instal ESP32 Core

Instal versi yang digunakan pada proyek ini:

``` bash
arduino-cli core install esp32:esp32@3.3.12
```

Periksa versi yang terpasang:

``` bash
arduino-cli core list
```

Pastikan terlihat:

``` text
esp32:esp32  3.3.12
```

Lokasi core yang digunakan pada contoh ini:

``` text
/root/.arduino15/packages/esp32/hardware/esp32/3.3.12
```

## 5. Periksa dan siapkan arsip toolchain

Toolchain yang digunakan pada instalasi ini adalah:

``` text
esp-x32/2601
```

Cari arsip compiler ARM64:

``` bash
find /root/.arduino15/staging/packages \
  -type f -name 'xtensa-esp-elf*aarch64*.tar.gz'
```

Nama arsip yang ditemukan pada instalasi ini:

``` text
xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz
```

Atur variabel arsip:

``` bash
A="/root/.arduino15/staging/packages/xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz"
```

Pastikan arsip tersedia:

``` bash
ls -lh "$A"
```

Jika arsip tidak ada, periksa instalasi ESP32 Core dan unduhan paket
Arduino. Jangan melanjutkan proses pemulihan dengan file arsip yang
tidak tersedia.

Ekstrak ke folder pemeriksaan terpisah:

``` bash
mkdir -p /tmp/xtensa_restore
tar -xzf "$A" -C /tmp/xtensa_restore
```

Tes compiler asli:

``` bash
/tmp/xtensa_restore/xtensa-esp-elf/bin/xtensa-esp-elf-gcc-14.2.0 --version
```

Hasil yang diharapkan memuat versi `14.2.0`.

## 6. Pulihkan broken symbolic link

### 6.1 Tentukan lokasi toolchain

``` bash
TOOL="/root/.arduino15/packages/esp32/tools/esp-x32/2601"
```

Pastikan folder tersebut tersedia:

``` bash
ls -lah "$TOOL/bin"
```

### 6.2 Periksa target symbolic link G++

``` bash
readlink "$TOOL/bin/xtensa-esp-elf-g++"
```

Pada kasus yang diperbaiki, link mengarah ke folder sementara seperti:

``` text
/root/.arduino15/tmp/package-XXXXXXXXXX/xtensa-esp-elf/bin/...
```

Angka `XXXXXXXXXX` adalah ID paket sementara dan dapat berbeda.

### 6.3 Pulihkan folder target secara otomatis

Ambil lokasi folder sementara dari symbolic link yang benar-benar ada:

``` bash
LINK="$(readlink "$TOOL/bin/xtensa-esp-elf-g++")"
PKGROOT="${LINK%%/xtensa-esp-elf/*}"

printf 'Target pemulihan: %s\n' "$PKGROOT"
```

Validasi lokasi agar tidak menyalin ke folder yang salah:

``` bash
case "$PKGROOT" in
  /root/.arduino15/tmp/package-*) echo "Lokasi target valid" ;;
  *) echo "Target tidak sesuai. Hentikan proses."; exit 1 ;;
esac
```

Salin kembali folder toolchain hasil ekstraksi ke lokasi target:

``` bash
mkdir -p "$PKGROOT"
cp -a /tmp/xtensa_restore/xtensa-esp-elf "$PKGROOT/"
```

### 6.4 Tes GCC dan G++

``` bash
"$TOOL/bin/xtensa-esp-elf-gcc" --version
```

``` bash
"$TOOL/bin/xtensa-esp-elf-g++" --version
```

Keduanya harus dapat dijalankan dan menampilkan versi compiler `14.2.0`.

Periksa apakah link dapat diikuti:

``` bash
readlink -e "$TOOL/bin/xtensa-esp-elf-g++"
```

Jika `readlink -e` mengeluarkan lokasi file, target link sudah
ditemukan.

> **Mengapa langkah ini diperlukan?** Pada kasus sebelumnya, beberapa
> symbolic link di direktori toolchain menunjuk ke file di
> `/root/.arduino15/tmp/package-.../`, tetapi direktori sementara
> tersebut sudah hilang. Akibatnya Arduino CLI tidak dapat menemukan
> compiler. Mengembalikan folder `xtensa-esp-elf` dari arsip ke target
> yang ditunjuk link memperbaiki masalah tersebut.

## 7. Periksa library sketch

Pasang library tambahan yang diperlukan proyek jika belum tersedia:

``` bash
arduino-cli lib install "Adafruit SSD1306"
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "ArduinoJson"
arduino-cli lib install "UniversalTelegramBot"
```

Periksa library:

``` bash
arduino-cli lib list
```

Library seperti `WiFi`, `BluetoothSerial`, `WebServer`, `HTTPClient`,
`Preferences`, `SPI`, dan `FS` disediakan oleh ESP32 Core.

**Catatan versi:** log proyek sebelumnya menunjukkan
`ArduinoJson 7.4.3`. Jika sketch atau kode proyek secara khusus
membutuhkan ArduinoJson versi 6, pasang versi yang sesuai dengan kode,
misalnya:

``` bash
arduino-cli lib install "ArduinoJson@6.21.5"
```

Jangan memasang beberapa versi secara manual ke folder yang sama.
Periksa hasil `arduino-cli lib list` setelah instalasi.

## 8. Kompilasi sketch ESP32

Contoh lokasi sketch:

``` text
/root/ESP32_SmartDisplay_MotoNav_Combined
```

Pastikan folder tersebut berisi file `.ino` dengan nama folder dan file
utama yang sesuai aturan Arduino.

Jalankan kompilasi:

``` bash
arduino-cli compile \
  --jobs 3 \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
  --build-path /root/arduino_build \
  --export-binaries \
  /root/ESP32_SmartDisplay_MotoNav_Combined
```

Keterangan:

-   `--jobs 3`: membatasi pekerjaan paralel agar beban RAM lebih
    terkendali.
-   `--fqbn`: menentukan board ESP32 dan partisi `huge_app`.
-   `--build-path`: menentukan folder hasil build.
-   `--export-binaries`: meminta Arduino CLI mengekspor file binary
    hasil kompilasi.

Jika berhasil, Arduino CLI biasanya menampilkan `Done compiling.` dan
kode keluar `0`.

Periksa kode keluar segera setelah perintah compile selesai:

``` bash
echo $?
```

-   `0`: perintah terakhir berhasil.
-   Selain `0`: perintah terakhir gagal.

Jangan menjalankan perintah lain sebelum memeriksa `echo $?`, karena
perintah lain akan mengganti status keluar yang tersimpan.

## 9. Cari file merged.bin

Cari file firmware gabungan:

``` bash
find /root/arduino_build -type f -name "*.merged.bin"
```

Tampilkan ukuran file:

``` bash
find /root/arduino_build -type f -name "*.merged.bin" \
  -exec ls -lh {} \;
```

Jika file ditemukan, catat lokasi lengkapnya. File `.merged.bin` adalah
hasil gabungan image firmware yang disiapkan oleh proses build untuk
konfigurasi board tersebut.

Jika tidak ditemukan, periksa isi folder build:

``` bash
find /root/arduino_build -maxdepth 3 -type f
```

Pastikan kompilasi benar-benar berhasil dan opsi ekspor binary
digunakan.

## 10. Script pemulihan otomatis

Bagian ini membuat script `fix_xtensa.sh` untuk memulihkan folder target
toolchain dari arsip yang sudah ada.

Buat file:

``` bash
nano fix_xtensa.sh
```

Isi dengan:

``` bash
#!/bin/bash
set -euo pipefail

echo "=================================="
echo " ESP32 XTENSA TOOLCHAIN REPAIR"
echo "=================================="

TOOL="/root/.arduino15/packages/esp32/tools/esp-x32/2601"
ARCHIVE="/root/.arduino15/staging/packages/xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz"
RESTORE="/tmp/xtensa_restore"

# Periksa direktori toolchain
if [ ! -d "$TOOL/bin" ]; then
    echo "[ERROR] Folder toolchain tidak ditemukan: $TOOL"
    exit 1
fi

# Periksa arsip
if [ ! -f "$ARCHIVE" ]; then
    echo "[ERROR] Arsip toolchain tidak ditemukan: $ARCHIVE"
    exit 1
fi

# Baca target symbolic link
LINK="$(readlink "$TOOL/bin/xtensa-esp-elf-g++")"
PKGROOT="${LINK%%/xtensa-esp-elf/*}"

# Validasi target
case "$PKGROOT" in
    /root/.arduino15/tmp/package-*) ;;
    *)
        echo "[ERROR] Target symbolic link tidak sesuai."
        echo "Target: $PKGROOT"
        exit 1
        ;;
esac

echo "[INFO] Target pemulihan: $PKGROOT"

# Ekstrak arsip ke folder sementara
mkdir -p "$RESTORE"
tar -xzf "$ARCHIVE" -C "$RESTORE"

# Pastikan compiler asli tersedia
SOURCE="$RESTORE/xtensa-esp-elf"

if [ ! -x "$SOURCE/bin/xtensa-esp-elf-gcc-14.2.0" ]; then
    echo "[ERROR] Compiler asli tidak ditemukan."
    exit 1
fi

# Pulihkan target link
mkdir -p "$PKGROOT"
cp -a "$SOURCE" "$PKGROOT/"

# Tes compiler
echo
echo "[INFO] Tes GCC:"
"$TOOL/bin/xtensa-esp-elf-gcc" --version

echo
echo "[INFO] Tes G++:"
"$TOOL/bin/xtensa-esp-elf-g++" --version

echo
echo "=================================="
echo " TOOLCHAIN REPAIR SUCCESS"
echo "=================================="
```

Simpan file, lalu beri izin eksekusi:

``` bash
chmod +x fix_xtensa.sh
```

Jalankan:

``` bash
./fix_xtensa.sh
```

**Batasan script:** script ini mengasumsikan versi core, nama arsip, dan
lokasi toolchain sama dengan konfigurasi yang dicatat di panduan. Jika
versi paket berubah, sesuaikan variabelnya dan periksa target symbolic
link terlebih dahulu.

## 11. Troubleshooting

### Error: `Tool ... xtensa-esp-elf-g++ is not exist`

Penyebab yang ditemukan pada kasus ini: target symbolic link compiler
mengarah ke folder sementara yang sudah tidak ada.

Solusi: 1. Pastikan arsip toolchain tersedia. 2. Ekstrak arsip ke
`/tmp/xtensa_restore`. 3. Baca target link dengan `readlink`. 4. Salin
folder `xtensa-esp-elf` ke folder target yang ditunjuk link. 5. Tes GCC
dan G++.

### Error: `readlink -e` tidak menghasilkan lokasi

Artinya link tidak dapat diikuti sampai file target. Periksa link:

``` bash
readlink "$TOOL/bin/xtensa-esp-elf-g++"
```

Periksa keberadaan target:

``` bash
ls -lah /root/.arduino15/tmp/
```

Lanjutkan pemulihan sesuai langkah 6.

### Error: `core update-index` gagal

Periksa jaringan dan DNS terlebih dahulu. Setelah koneksi normal,
ulangi:

``` bash
arduino-cli core update-index
```

### Error: `exit status 1`

Jangan langsung menyimpulkan toolchain rusak. Periksa pesan error
pertama yang muncul sebelum `Error during build`. Penyebabnya dapat
berupa kode sketch, library, konfigurasi board, atau toolchain.

### Peringatan `--build-cache-path` deprecated

Gunakan `--build-path` untuk menentukan folder hasil build. Peringatan
deprecation tersebut sendiri bukan bukti bahwa kompilasi gagal.

------------------------------------------------------------------------

## Checklist pemulihan cepat

Gunakan checklist ini setelah Linux di-reset:

-   [ ] Periksa `uname -m` menghasilkan `aarch64`.
-   [ ] Instal Arduino CLI dan cek `arduino-cli version`.
-   [ ] Jalankan `arduino-cli core update-index`.
-   [ ] Instal ESP32 Core `3.3.12`.
-   [ ] Pastikan arsip toolchain ARM64 tersedia.
-   [ ] Ekstrak arsip ke `/tmp/xtensa_restore`.
-   [ ] Pulihkan target symbolic link jika rusak.
-   [ ] Tes `xtensa-esp-elf-gcc --version`.
-   [ ] Tes `xtensa-esp-elf-g++ --version`.
-   [ ] Periksa library sketch.
-   [ ] Compile dengan FQBN dan partisi yang benar.
-   [ ] Cari file `.merged.bin`.

## Catatan backup

Simpan salinan arsip toolchain dan script pemulihan di penyimpanan yang
tidak ikut terhapus ketika lingkungan Linux di-reset.

Contoh lokasi backup setelah folder Android dapat diakses dari Linux:

``` text
Download/
├── xtensa-esp-elf-14.2.0_20260121-aarch64-linux-gnu.tar.gz
└── fix_xtensa.sh
```

Jangan mengandalkan folder `/tmp` sebagai tempat backup permanen karena
isinya dapat hilang ketika lingkungan Linux dimulai ulang atau
dibersihkan.
