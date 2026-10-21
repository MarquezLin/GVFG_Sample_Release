# GVFG Customer Sample

這個 Qt Demo 展示公開 Capture／Playback API 的使用：列舉裝置、開啟 channel、
啟停、選擇 YUY2／Y210、copy／zero-copy、影像預覽與可選音訊。
影音同步由 SDK 的 `gvfg_playback.dll` 負責。
Customer Demo 預設關閉影音同步，影像與音訊各自播放。
需要同步時，將啟動流程中 `gvfg_playback_set_sync_enabled()` 的參數改為 1。


## 1. 環境

- Windows 10／11 x64、相容的 GVFG driver 與硬體。
- CMake 3.16+、Qt 6 Widgets 與相容 toolchain。
- 同層 SDK source 模式需要 MSVC x64，以連結 supplied vendor 靜態 library。
- Qt 模組只需 Widgets。

## 2. 套件內容與相依

```text
include/  gvfg_capture.h、gvfg_playback.h、gvfg_preview.h、gvfg_audio_playback.h
bin/      gvfg.dll、gvfg_playback.dll
lib/      gvfg.lib、gvfg_playback.lib（及對應 GNU 匯入庫）
src/      Qt Demo source
```

連結 `gvfg.lib` 與 `gvfg_playback.lib`；執行時部署 `gvfg.dll` 與 `gvfg_playback.dll`。
`gvfg_preview.h` 與 `gvfg_audio_playback.h` 提供 Playback 使用的資料型別。

## 3. 建置方式

在 Qt Creator 開啟此目錄的 CMakeLists.txt，選 Windows x64 Qt kit，或執行：

```bat
cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64
cmake --build build_release --target gvfg_qt_preview
```

若使用 Visual Studio 多組態 generator，改用對應 generator 並在 build 指定 `--config Release`。

SDK 選擇規則：有 GVFG_SAMPLE_SDK_BUILD_DIR 時使用該 build；否則找到同層
GVFG_Standalone/GVFG_SDK 就一起建置 source；兩者都沒有才使用套件 include/bin/lib。
要強制驗證套件來源，可 configure 時加 `-DGVFG_SAMPLE_SDK_SOURCE_DIR=`。
明確使用其他 SDK 時，GVFG_SAMPLE_SDK_BUILD_DIR 與 GVFG_SAMPLE_SDK_SOURCE_DIR 必須指向配套版本。

## 4. 從 source 看 API 用法

| 檔案 | 展示內容 |
|---|---|
| `src/playback.cpp` | create、attach、default_config、start、submit、reset、stop、destroy 的薄包裝 |
| `src/capture_controller.cpp` | Capture lifecycle、video／audio read、先完成使用／複製再 release、事件處理 |
| `src/capture_controller_status.cpp` | 公開訊號／格式／FPS 與基本 API 錯誤 |
| `src/mainwindow.cpp`、`previewwindow.cpp` | UI、HWND 與預覽視窗生命週期 |

通常操作：選裝置及格式 → 選擇是否音訊 → Start → Stop。
目前一般 UI 只顯示 CH0；不要從 SDK 支援兩條 channel 推論 Demo 已提供完整雙 channel UI。
隱藏預覽不停止影像時間軸提交；影音模式仍需兩路資料。停止會 join read workers 並結束播放，再回收擷取資源。

Demo 顯示基本狀態、版本及 API 錯誤。
需要完整使用流程、模式、ownership 與錯誤處理時，讀 [公開播放手冊](../../GVFG_Standalone/docs/GVFG_PLAYBACK_API.md)。
獨立套件可參閱 SDK 安裝文件中的同名手冊；上面的 source-tree 連結只在完整 workspace 有效。

## 5. Release 封裝

先手動建置 Release，再執行：

```bat
package_release.bat "C:\path\to\gvfg_qt_preview.exe" "C:\Qt\6.10.2\msvc2022_64\bin" "C:\path\to\packages"
```

不指定參數時使用腳本定義的 Qt Creator Release build 路徑。
腳本不建置、不搜尋其他 build，從 EXE 同目錄取 gvfg.dll／gvfg_playback.dll，
再以匹配的 windeployqt 打包 Qt／runtime。
SDK include／匯入庫與 Demo source 是開發套件內容；app runtime zip 與完整開發套件不是同一件事。
