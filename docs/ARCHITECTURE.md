# 아키텍처

## 계층

```text
Application
├─ ImmediateUi + SkiaRenderer
├─ SDL3 window/input
├─ NativeDialogs / SingleInstance
└─ Core
   ├─ PortablePaths + Config + Logger
   ├─ HttpClient + RepositoryParser + Sha1 + ArchiveExtractor
   ├─ SdkManager + AvdManager + EmulatorManager + AdbManager
   ├─ ApkInspector + HostInfo
   └─ Process + TaskQueue
```

Skia는 그림을 그리고 SDL3가 PortableAVM 런처 창, 이벤트, 키보드·마우스, 고 DPI 정보를 담당합니다. GUI는 retained widget toolkit이 아니라 PortableAVM 용도에 맞춘 immediate-mode 계층입니다.

## Android 도구 경계

Android SDK/JDK/AVD 관련 자식 프로세스에는 `Data` 아래의 포터블 환경 블록을 전달합니다. 시스템 Android SDK 경로를 우선 사용하지 않으며, 시스템 환경 변수를 영구 변경하지 않습니다.

## 프로세스 경계

PortableAVM은 `emulator.exe`를 직접 자식 프로세스로 시작합니다. 별도 명령 프롬프트에서 Emulator를 실행하거나 Emulator 창을 PortableAVM에 `SetParent`로 붙이지 않습니다. Android Emulator는 자체 네이티브 창을 표시하고 PortableAVM은 자신이 시작한 PID와 종료만 관리합니다. Windows에서는 `CreateProcessW()`로 `emulator.exe`를 콘솔 없이 직접 시작하고, 소유한 장기 실행 프로세스를 Job Object에 연결해 PortableAVM 종료 시 정리합니다.

SDK 라이선스 검토는 예외적으로 사용자 입력이 필요한 콘솔을 열지만 `cmd.exe /D /C`를 사용하므로 `sdkmanager --licenses` 완료 후 콘솔이 자동 종료됩니다.
