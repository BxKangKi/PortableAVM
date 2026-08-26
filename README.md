# PortableAVM 0.3.46

PortableAVM은 공식 Android Emulator/SDK를 포터블 `Data/` 트리에서 관리하고 실행하는 C++ 런처입니다. Windows에서는 별도 터미널이나 Host helper 없이 PortableAVM 본체가 `CreateProcessW()`로 `emulator.exe`를 직접 실행합니다. Emulator 화면은 PortableAVM 안에 임베드하지 않고 Android Emulator의 별도 GUI 창으로 표시됩니다.

자세한 사용 설명과 변경 이력은 [`README.ko.md`](README.ko.md)를 참고하세요.

## 0.3.46 device and SDK management

PortableAVM now manages multiple virtual devices instead of treating one AVD as the entire application state. The Virtual Devices page lists every AVD under `Data/AVD`, lets the user select one to launch or edit, and supports creating and deleting devices independently. The SDK Manager page lists installed Android Emulator, Platform Tools, and System Images with their package identifiers/revisions, supports installation, and can remove selected components. System Images that are referenced by an existing AVD are protected from deletion until the dependent AVD is removed or recreated with another image.

## 프로젝트 폴더 역할

| 경로 | 역할 |
| --- | --- |
| `CMakeLists.txt` | 전체 CMake 빌드 구성, 실행 파일/테스트/설치 규칙 |
| `src/` | PortableAVM C++ 소스 |
| `src/core/` | SDK, AVD, JDK, 프로세스, 설정, 로그, 경로 등 플랫폼 독립 핵심 로직 |
| `src/platform/` | Windows/Linux 네이티브 기능과 단일 인스턴스/대화상자 등 플랫폼 코드 |
| `src/ui/` | SDL3 + Skia 기반 UI 렌더링과 입력 위젯 |
| `tests/` | 코어 회귀 테스트 |
| `scripts/` | Windows/Linux 빌드, 도구 bootstrap, Visual Studio 탐지, 소스 검증 스크립트 |
| `cmake/` | CMake 보조 모듈 |
| `resources/` | 앱 아이콘과 배포 시 필요한 정적 리소스 |
| `resources/Data/lang/` | 최초 배포용 언어 파일 템플릿. 빌드/설치 시 `Data/lang/`으로 복사 |
| `docs/` | 아키텍처, 빌드, ARM/무결성 관련 개발 문서 |
| `build/` | 로컬 빌드 도구, Skia/SDL 의존성, CMake 산출물. 소스 ZIP에는 포함하지 않음 |

## 런타임 `Data/` 폴더 역할

`Data/` 아래 폴더는 **필요한 기능을 처음 사용할 때 생성**합니다. 존재하지 않는 경로를 사용하기 직전에 PortableAVM이 `create_directories()`로 생성합니다. 불필요한 빈 폴더는 시작 시 미리 만들지 않습니다.

| 경로 | 역할 / 생성 시점 |
| --- | --- |
| `Data/lang/` | UI 번역 `.lang` 파일. 배포 시 기본 생성 |
| `Data/Configs/` | `settings.ini`, 선택적 `PortableAVM.ico`. 설정 저장 시 생성 |
| `Data/Runtime/Android/sdk/` | Android SDK, platform-tools, emulator, system images. SDK 작업 시 생성 |
| `Data/Runtime/jdk/` | 사용자가 가져온 JDK. JDK import 시 생성 |
| `Data/AVD/` | PortableAVM이 관리하는 여러 AVD의 `.avd/` 데이터와 descriptor `.ini`. 각 기기를 독립적으로 생성/선택/삭제 |
| `Data/Logs/` | PortableAVM 및 `emulator-process.log`. 로그 기록/Emulator 실행 시 생성 |
| `Data/Locks/` | 단일 인스턴스 잠금 파일. 앱 시작 시 생성 |
| `Data/temp/` | ZIP 추출/JDK import 등 임시 작업. 해당 작업 시 생성 |
| `Data/downloads/` | 다운로드가 필요한 기능의 저장 위치 |
| `Data/cache/` | 재사용 가능한 런타임 캐시 |
| `Data/home/android/` | Android command-line tools 사용자 홈 |
| `Data/home/emulator/` | Android Emulator 사용자 홈 |
| `Data/home/adb/` | ADB vendor key/home 격리 |
| `Data/home/user/` | Android 자식 프로세스용 포터블 HOME/USERPROFILE |
| `Data/home/gradle/` | 포터블 `GRADLE_USER_HOME` |
| `Data/windows/roaming/` | 자식 프로세스용 격리 `APPDATA` |
| `Data/windows/local/` | 자식 프로세스용 격리 `LOCALAPPDATA` |
| `Data/bin/` | PortableAVM 런타임 보조 바이너리를 둘 수 있는 예약 위치. 실제 사용 전에는 생성하지 않음 |

## Emulator 실행 구조 (Windows)

```text
PortableAVM.exe (WIN32 GUI)
  └─ CreateProcessW(CREATE_NO_WINDOW | CREATE_SUSPENDED)
      └─ emulator.exe
          └─ Android Emulator / QEMU GUI
```

PortableAVM은 `emulator.exe`를 Job Object에 연결한 뒤 실행을 재개합니다. 콘솔 창은 억제하고, Job 안에서 만들어지는 실제 Emulator top-level GUI 창은 찾아서 표시/복원합니다. 따라서 터미널을 내장하거나 `cmd.exe`에서 Emulator를 실행할 필요가 없습니다.
