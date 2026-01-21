# ESP32 OTA Automatic Release Tool

Script tự động hóa quy trình build và release firmware ESP32 lên GitHub để hỗ trợ OTA (Over-The-Air) updates.

## 🎯 Chức năng

- ✅ Build Arduino sketch (.ino) thành file binary (.bin)
- ✅ Tự động tạo GitHub Release với tag version
- ✅ Upload file .bin lên GitHub Release
- ✅ Cập nhật file `version.txt`
- ✅ Tự động cập nhật version và URLs trong code Arduino
- ✅ Commit và push changes lên GitHub
- ✅ Hỗ trợ increment version tự động (major/minor/patch)

## 📋 Yêu cầu

### 1. Python 3.7+
```bash
python --version
```

### 2. Arduino CLI
Cài đặt Arduino CLI: https://arduino.github.io/arduino-cli/latest/installation/

**Windows (PowerShell):**
```powershell
# Tải về arduino-cli
Invoke-WebRequest -Uri "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip" -OutFile "arduino-cli.zip"

# Giải nén
Expand-Archive arduino-cli.zip -DestinationPath "$env:ProgramFiles\Arduino CLI"

# Thêm vào PATH
$env:Path += ";$env:ProgramFiles\Arduino CLI"
```

### 3. Cấu hình Arduino CLI
```bash
# Khởi tạo config
arduino-cli config init

# Thêm ESP32 board manager URL
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Cập nhật index
arduino-cli core update-index

# Cài đặt ESP32 platform
arduino-cli core install esp32:esp32

# Kiểm tra
arduino-cli board listall esp32
```

### 4. GitHub Personal Access Token

Tạo token tại: https://github.com/settings/tokens

**Quyền cần thiết:**
- ✅ `repo` (Full control of private repositories)
- ✅ `workflow` (Update GitHub Action workflows)

## 🚀 Cài đặt

### 1. Clone hoặc copy script
```bash
cd "d:\SCA\Automation Release"
```

### 2. Cài đặt Python dependencies
```bash
pip install -r requirements.txt
```

### 3. Tạo file config
```bash
# Copy file mẫu
copy config.json.example config.json

# Sửa config.json với thông tin của bạn
notepad config.json
```

### 4. Cấu hình `config.json`

```json
{
  "github_token": "ghp_your_github_token_here",
  "github_repo": "lehieu2909/test_OTA",
  "arduino_cli_path": "arduino-cli",
  "board_fqbn": "esp32:esp32:esp32s3",
  "sketch_path": "d:\\SCA\\example\\Test_OTA\\test_OTA\\test_OTA.ino",
  "version_file": "version.txt",
  "auto_increment": true,
  "update_ino_file": true
}
```

**Giải thích các tham số:**

| Tham số | Mô tả | Ví dụ |
|---------|-------|-------|
| `github_token` | GitHub Personal Access Token | `ghp_xxxxx` |
| `github_repo` | Repository (owner/name) | `lehieu2909/test_OTA` |
| `arduino_cli_path` | Đường dẫn arduino-cli | `arduino-cli` hoặc đường dẫn đầy đủ |
| `board_fqbn` | Board Fully Qualified Board Name | `esp32:esp32:esp32s3` |
| `sketch_path` | Đường dẫn file .ino | `d:\\SCA\\example\\Test_OTA\\test_OTA\\test_OTA.ino` |
| `version_file` | File lưu version | `version.txt` |
| `auto_increment` | Tự động tăng version | `true` hoặc `false` |
| `update_ino_file` | Tự động cập nhật .ino | `true` hoặc `false` |

**Tìm FQBN của board:**
```bash
# List tất cả boards đã cài
arduino-cli board listall

# Ví dụ kết quả:
# Board Name              FQBN
# ESP32S3 Dev Module      esp32:esp32:esp32s3
# ESP32 Dev Module        esp32:esp32:esp32
```

## 📝 Cách sử dụng

### 1. Release với auto-increment (Patch)
```bash
python auto_release.py
```
Version sẽ tự động tăng: `1.0.0` → `1.0.1`

### 2. Release với increment type
```bash
# Patch: 1.0.0 → 1.0.1
python auto_release.py -i patch

# Minor: 1.0.5 → 1.1.0
python auto_release.py -i minor

# Major: 1.2.3 → 2.0.0
python auto_release.py -i major
```

### 3. Release với version cụ thể
```bash
python auto_release.py -v 2.5.0
```

### 4. Skip build (dùng binary có sẵn)
```bash
python auto_release.py --skip-build
```

### 5. Custom config file
```bash
python auto_release.py -c custom_config.json
```

### 6. Xem help
```bash
python auto_release.py --help
```

## 🔄 Quy trình hoạt động

1. **Đọc version hiện tại** từ `version.txt`
2. **Xác định version mới** (auto-increment hoặc manual)
3. **Xác nhận** từ người dùng
4. **Cập nhật files**:
   - `version.txt` → version mới
   - `test_OTA.ino` → `currentFirmwareVersion` và `firmwareUrl`
5. **Build Arduino sketch** → `.ino.bin`
6. **Tạo GitHub Release** với tag `v{version}`
7. **Upload binary** lên release
8. **Commit & push** changes lên GitHub

## 📂 Cấu trúc Files

```
Automation Release/
├── auto_release.py          # Script chính
├── config.json             # Cấu hình (tạo từ .example)
├── config.json.example     # Template cấu hình
├── requirements.txt        # Python dependencies
└── README.md              # Hướng dẫn này
```

## 🐛 Troubleshooting

### Lỗi: "arduino-cli not found"
```bash
# Kiểm tra arduino-cli đã được cài
arduino-cli version

# Nếu chưa có, cài đặt theo hướng dẫn trên
# Hoặc chỉ định đường dẫn đầy đủ trong config.json
"arduino_cli_path": "C:\\Program Files\\Arduino CLI\\arduino-cli.exe"
```

### Lỗi: "Board esp32:esp32:esp32s3 not installed"
```bash
# Cài đặt ESP32 platform
arduino-cli core install esp32:esp32

# Kiểm tra
arduino-cli board listall esp32
```

### Lỗi: "Failed to create release: 401 Unauthorized"
- Kiểm tra GitHub token có đúng không
- Kiểm tra token có quyền `repo` không
- Token có thể đã hết hạn

### Lỗi: "Build failed"
```bash
# Thử build thủ công để xem lỗi chi tiết
arduino-cli compile --fqbn esp32:esp32:esp32s3 "d:\SCA\example\Test_OTA\test_OTA\test_OTA.ino"
```

### Lỗi: "Could not find generated .bin file"
- Kiểm tra build có thành công không
- Kiểm tra đường dẫn trong `config.json`
- File .bin thường nằm trong `build/esp32.esp32.esp32s3/`

## 🔐 Bảo mật

⚠️ **Quan trọng:**
- **KHÔNG** commit file `config.json` có chứa GitHub token
- Thêm `config.json` vào `.gitignore`
- Chỉ commit `config.json.example` (không có token)

```bash
# .gitignore
config.json
*.pyc
__pycache__/
```

## 📖 Ví dụ Output

```
============================================================
ESP32 OTA Automatic Release
============================================================
📌 Current version: 1.0.0
🎯 New version: 1.0.1

⚠️  Proceed with release v1.0.1? (yes/no): yes
✅ Updated version.txt to version 1.0.1
✅ Updated .ino file with version 1.0.1
🔨 Building Arduino sketch: d:\SCA\example\Test_OTA\test_OTA\test_OTA.ino
✅ Build successful!
📦 Binary file: d:\SCA\example\Test_OTA\test_OTA\build\esp32.esp32.esp32s3\test_OTA.ino.bin
🚀 Creating GitHub release v1.0.1
✅ Release created: https://github.com/lehieu2909/test_OTA/releases/tag/v1.0.1
📤 Uploading binary file...
✅ Binary uploaded: https://github.com/lehieu2909/test_OTA/releases/download/v1.0.1/test_OTA.ino.bin
📝 Committing version changes...
✅ Version changes committed and pushed

============================================================
✨ Release completed successfully!
============================================================
Version: 1.0.1
Release URL: https://github.com/lehieu2909/test_OTA/releases/tag/v1.0.1
Binary: test_OTA.ino.bin
============================================================
```

## 🔗 Links hữu ích

- [Arduino CLI Documentation](https://arduino.github.io/arduino-cli/)
- [GitHub REST API - Releases](https://docs.github.com/en/rest/releases/releases)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [Creating GitHub Personal Access Tokens](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/creating-a-personal-access-token)

## 📄 License

MIT License - Tự do sử dụng và chỉnh sửa theo nhu cầu.
