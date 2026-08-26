# PortableAVM 0.3.46

PortableAVM은 Android SDK의 공식 Emulator/AVD 도구를 포터블 디렉터리 안에서 관리하기 위한 비공식 C++20 데스크톱 프런트엔드입니다. PortableAVM 자체 GUI는 **Qt를 사용하지 않으며**, Skia의 CPU 래스터 렌더러와 SDL3의 창·입력 계층을 사용합니다.

> 이 프로젝트는 Google, Android, Samsung, BlueStacks 또는 게임 개발사와 제휴하거나 인증받은 제품이 아닙니다. Android SDK·Emulator·시스템 이미지·Google Play 구성요소·JDK·게임 파일은 소스 및 배포 패키지에 포함하지 않습니다.

## 0.3.46 다중 가상 기기 / SDK 관리

- `Data/AVD/` 아래의 여러 가상 기기를 목록으로 표시하고 각각 선택해 실행, 편집, 재생성, 삭제할 수 있습니다.
- 새 가상 기기는 기존 기기와 독립된 이름으로 생성되며 선택된 기기의 API, ABI, RAM, CPU, 해상도, DPI 등의 설정을 실제 `config.ini`에서 읽어옵니다.
- `SDK 관리` 화면에서 설치된 Android Emulator, Platform Tools, System Image를 목록으로 확인하고 버전/패키지 경로를 확인할 수 있습니다.
- Emulator와 System Image를 선택해 삭제할 수 있습니다. 기존 가상 기기가 사용하는 System Image는 실수로 삭제되지 않도록 차단합니다.
- 삭제 작업은 확인 대화상자를 거치며, Emulator 실행 중에는 관련 삭제 기능을 비활성화합니다.
- 초기 설치, 가상 기기, SDK 관리, 앱, 로그, 언어 화면의 역할을 분리해 한 화면에서 한 가지 작업에 집중하도록 UI를 재구성했습니다.

## 0.3.45 일반 Android 앱 호환성 / 공식 Phone 프로필

- 새 설정 기본값을 Android 11(API 30) + `google_apis_playstore` + 호스트 권장 ABI로 조정했습니다.
- 설치 화면에 특정 앱 이름을 사용하지 않는 `일반 앱 호환성 권장값`을 추가했습니다.
- `avdmanager list device`에서 공식 Phone 하드웨어 프로필을 조회하고 가상 기기 관리 화면에서 선택할 수 있습니다. Tablet, Wear OS, TV, Automotive, Desktop/XR 계열 프로필은 Phone 목록에서 제외합니다.
- AVD 생성 시 공식 `avdmanager create avd --device <profile>`을 먼저 사용해 프로필 속성을 생성합니다. 포터블 SDK 경로 또는 구버전 Command-line Tools의 `devices.xml` 문제로 실패하면 기존 직접 생성 방식으로 자동 fallback합니다.
- fallback AVD에는 전화기형 일반 하드웨어 기능(GPS, 가속도계, 자이로, 근접/조도/자기장 센서, GSM modem, 카메라 등)을 설정하되 실기기 신원·fingerprint·attestation 속성은 기록하지 않습니다.

## 핵심 원칙

- 모든 PortableAVM 관리 데이터는 실행 파일 옆 `Data/` 아래에 둡니다.
- 시스템 전역 환경 변수, 사용자 환경 변수, 레지스트리의 Android 설정을 수정하지 않습니다.
- Android Command-line Tools는 사용자가 Android Developers에서 ZIP을 직접 내려받은 뒤 PortableAVM에서 선택해 가져옵니다.
- 저장소 메타데이터의 크기와 SHA 체크섬을 검사하고, 스테이징 디렉터리에서 구조와 경로를 검증한 뒤 설치합니다.
- `sdkmanager --licenses`는 사용자가 직접 응답하는 대화형 터미널로 실행하며 자동 동의하지 않습니다.
- AVD의 화면·RAM·CPU·저장공간·GPU 같은 하드웨어 설정은 지원하지만, 기기 인증서·빌드 지문·Play Integrity 판정·DRM·안티치트를 위조하거나 우회하지 않습니다.

## 포터블 디렉터리

```text
PortableAVM[.exe]
Data/
├─ bin/                         # 프로젝트가 관리하는 보조 바이너리 자리
├─ configs/                     # settings.ini 등 사용자 설정
├─ runtime/
│  ├─ Android/sdk/              # 공식 SDK 패키지
│  └─ jdk/                      # 사용자가 가져온 JDK
├─ avd/                         # AVD 정의와 디스크 이미지
├─ home/
│  ├─ android/
│  ├─ emulator/
│  ├─ adb/
│  ├─ gradle/
│  └─ user/
├─ windows/{roaming,local}/
├─ downloads/
├─ cache/
├─ temp/
├─ logs/
└─ locks/
```

자식 프로세스에만 `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_USER_HOME`, `ANDROID_EMULATOR_HOME`, `ANDROID_AVD_HOME`, `ANDROID_PREFS_ROOT`, `ADB_VENDOR_KEYS`, `HOME`, `USERPROFILE`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`, `GRADLE_USER_HOME` 등을 포터블 경로로 전달합니다. Windows·GPU 드라이버·백신·오류 보고 도구처럼 프로젝트 밖의 구성요소가 별도 시스템 캐시를 만들 가능성까지 제거할 수는 없습니다.

## UI에서 제공하는 기능

- 사용자가 내려받은 Android Command-line Tools ZIP 가져오기 및 구조 검증
- JDK 폴더를 `Data/Runtime/jdk`로 가져오기
- `sdkmanager` 패키지 목록·설치·업데이트 및 대화형 라이선스 처리
- Emulator 버전/시스템 이미지/API/ABI 선택
- 공식 `avdmanager` 하드웨어 프로필 조회 및 AVD 생성·삭제
- RAM, CPU 코어, 해상도, DPI, 데이터 파티션, GPU 모드, 오디오, 네트워크, 프록시, DNS, 스냅샷 설정
- APK의 `lib/<abi>/` 분석과 선택한 시스템 이미지의 ABI 호환성 표시
- AVD 실행·중지, 포터블 전용 ADB 포트, 합법적으로 확보한 APK 설치
- Windows에서 PortableAVM이 `CreateProcessW()`로 Android Emulator를 콘솔 없이 직접 실행하고 GUI는 별도 창으로 표시

`ro.product.model`, `ro.product.brand`, `ro.product.manufacturer`, `ro.build.fingerprint`, verified-boot/attestation 관련 속성은 AVD 사용자 설정으로 기록하지 않으며 입력 시 차단합니다.

## ARM 지원 범위

- APK가 순수 Java/Kotlin이거나 여러 ABI를 포함하면 해당 구성을 표시합니다.
- ARM64 호스트에서는 공식 `arm64-v8a` 시스템 이미지를 네이티브 경로로 실행할 수 있습니다.
- x86-64 호스트에서 ARM64 이미지를 선택하면 이를 “ARM 네이티브”라고 표시하지 않습니다. 공식 Emulator가 해당 조합을 허용하는 범위에서 소프트웨어 CPU 에뮬레이션을 시도하고 큰 성능 경고를 표시합니다.
- 독점 ARM 변환 계층, 타사 Emulator에서 추출한 라이브러리, Houdini류 바이너리, 수정 시스템 이미지 또는 Google 인증 자료를 포함하지 않습니다.
- 앱·게임이 x86_64 네이티브 라이브러리를 제공하지 않고 공식 Emulator의 ARM 실행 경로도 맞지 않으면 실행되지 않을 수 있습니다.

자세한 구분은 [`docs/ARM_INTEGRITY.md`](docs/ARM_INTEGRITY.md)를 참고하십시오.

## Windows 빌드

프로젝트 루트에서 **이 한 줄만 실행하면 됩니다.**

```bat
build-windows.bat
```

이 배치 파일이 순서대로 다음 작업을 수행합니다.

1. `build/tools/`에 Portable Git, CMake, Python을 준비합니다. 시스템 PATH는 영구 수정하지 않습니다.
2. 설치된 Visual Studio 2026 (18.x)를 여러 방식으로 탐색하고 해당 개발 환경을 로드합니다. VS 2026 자체가 없을 때만 공식 Build Tools 설치를 시도합니다.
3. `build/deps/`에 Chromium `depot_tools`, Skia, SDL3, curl 소스를 내려받습니다.
4. Skia CPU raster 라이브러리를 빌드합니다.
5. CMake가 SDL3/curl과 PortableAVM을 Release 구성으로 빌드합니다.
6. 코어 테스트를 실행하고 `dist-windows-x64/` 또는 `dist-windows-arm64/`에 설치합니다.

클린 빌드:

```bat
build-windows.bat -Clean
```

Windows on ARM64 대상:

```bat
build-windows.bat arm64 -Clean
```

이미 동일한 Skia 출력이 준비되어 있을 때만 의존성 재빌드를 건너뛸 수 있습니다.

```bat
build-windows.bat -SkipDependencyBuild
```

기본 의존성 ref는 Skia `chrome/m126`, SDL `release-3.2.0`, curl `curl-8_11_1`이며 `PAVM_SKIA_REF`, `PAVM_SDL_REF`, `PAVM_CURL_REF` 환경변수로 명시적으로 바꿀 수 있습니다.

> `cl.exe`가 없으면 Visual Studio 2026 설치 자체는 그대로 사용하되 빌드를 중단하고, 그 인스턴스에 **Desktop development with C++** workload를 추가하라는 메시지를 표시합니다. 기존 VS 2026을 임의로 재설치하지 않습니다.

## Linux 빌드

```bash
chmod +x scripts/build-linux.sh
./scripts/build-linux.sh --install-deps
```

이미 의존 패키지가 설치되어 있으면 다음처럼 실행합니다.

```bash
./scripts/build-linux.sh
```

출력은 `dist-linux/`입니다. Linux 포터블성은 glibc·libcurl·창 시스템 등 빌드 호스트의 시스템 ABI에 영향을 받으므로, 배포 대상과 같거나 더 오래된 배포판에서 빌드하는 방식을 권장합니다.

## 소스 정책 검사와 코어 테스트

```bash
python3 scripts/validate-source.py --root .
./scripts/build-core-tests.sh
```

정책 검사기는 다음을 실패로 처리합니다.

- 코드·CMake·빌드 스크립트의 Qt 의존성
- SDK 약관의 자동 일괄 동의
- 시스템/사용자 영구 환경 변수 변경
- APK, 시스템 이미지, SDK/JDK 아카이브, 인증키, 실행 바이너리의 소스 번들링
- 공식 Android 저장소 URL 또는 AVD 식별 속성 차단 로직 누락

## Launcher 방식

PortableAVM은 Android Emulator 화면을 자체 창에 내장하지 않습니다. 실행 버튼을 누르면 PortableAVM이 C++ `CreateProcessW()`로 `emulator.exe`를 직접 자식 프로세스로 시작하고, Android Emulator는 별도의 네이티브 창으로 표시됩니다. 별도 명령 프롬프트나 Host helper를 사용하지 않으며, PortableAVM이 Job Object로 전체 Emulator 프로세스 트리를 관리합니다.

## 게임 호환성

게임 실행 여부는 ABI, Android API, 그래픽 API, Google Play 서비스, 지역·계정 제공 범위, 앱 개발사의 Emulator/루팅/무결성 정책에 따라 달라집니다. 특정 게임이나 업데이트 버전의 실행을 보장하지 않습니다. Play Integrity, DRM, 안티치트 또는 앱의 기기 제한을 우회하는 기능은 제공하지 않습니다.

## 라이선스

- PortableAVM 자체 소스: MIT (`LICENSE`)
- Skia: 해당 checkout의 `LICENSE`
- SDL3: 해당 checkout의 `LICENSE.txt`
- curl: 해당 checkout의 `COPYING`
- Android SDK/Emulator/시스템 이미지: 각 패키지의 Google 약관 및 라이선스

빌드 스크립트는 제3자 라이선스 사본과 정확한 checkout 커밋을 결과물에 복사합니다. 배포 전 [`LEGAL.md`](LEGAL.md), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), 각 의존성의 실제 라이선스를 확인하십시오.


## Windows portable Python / Skia 주의사항

Windows용 Python embeddable 배포본은 기본 `pythonXY._pth`에서 `site` 초기화를 비활성화합니다. Skia의 `tools/git-sync-deps`는 일반 Python 시작 환경의 `exit()`를 사용하므로 PortableAVM 빌드 부트스트랩은 프로젝트 로컬 Python의 `import site`만 활성화합니다. 시스템 Python 설정이나 시스템 환경 변수는 변경하지 않습니다. 기존 `build/tools/python` 캐시도 빌드 시작 시 자동으로 복구됩니다.






### 0.3.41 콘솔 없는 EmulatorHost 실행 구조 (0.3.43에서 제거됨)

- 이 버전에서 실험했던 별도 EmulatorHost 구조는 0.3.43에서 제거했습니다. 현재는 PortableAVM 본체가 `emulator.exe`를 직접 관리합니다.
- Host와 emulator/QEMU 자식은 PortableAVM이 만든 동일한 Windows Job Object 안에서 추적되므로 종료 버튼 및 강제 종료 정책은 기존과 동일합니다.
- stdout/stderr는 계속 `Data/Logs/emulator-process.log`로 전달됩니다.

### 0.3.39 Emulator 콘솔 숨김 / GUI 유지

- Windows에서 `emulator.exe`의 콘솔 창은 `CREATE_NO_WINDOW`로 숨깁니다.
- Android Emulator의 실제 네이티브 GUI 창은 기존 `EnumWindows` + `SW_SHOW`/`SW_RESTORE` 경로로 계속 표시합니다.
- 즉 명령 프롬프트/콘솔은 보이지 않고 Emulator 화면만 별도 창으로 표시됩니다.

### 0.3.37 Windows Emulator 프로세스 트리 수명 수정

- Android Emulator의 `emulator.exe` 런처가 실제 QEMU 프로세스를 띄운 뒤 먼저 종료되어도 실행 실패로 오인하지 않습니다.
- Windows에서는 Emulator를 suspended 상태로 만든 뒤 Job Object에 먼저 연결하고 실행을 재개해 자식 프로세스까지 같은 Job에서 추적합니다.
- 실행 상태는 최초 런처 PID가 아니라 Job Object의 활성 프로세스 수를 기준으로 판정합니다.
- 종료 버튼은 기존처럼 Job 전체를 종료하므로 PortableAVM이 시작한 Emulator 프로세스 트리를 정리합니다.
- `ChildProcess` 이동 대입 시 기존 핸들을 먼저 정리해 Job/process handle 누수 가능성도 제거했습니다.

### 0.3.36 언어 전용 탭 / 텍스트 caret 이동

- 언어 선택을 설치 탭에서 분리해 왼쪽 고정 `언어` 탭으로 이동했습니다.
- 언어 리소스는 계속 `Data/lang/*.lang`에서 자동 탐색합니다.
- 텍스트 입력칸에서 마우스 클릭으로 caret 위치를 옮길 수 있습니다.
- Left/Right/Home/End/Delete/Backspace가 현재 caret 위치를 기준으로 동작합니다.
- UTF-8 문자 경계를 따라 이동/삭제하여 한글 등 다바이트 문자를 중간에서 깨뜨리지 않습니다.

### 0.3.35 빌드 결과 pause / Data 언어 리소스

- `build-windows.bat`은 성공/실패 exit code를 보존한 채 마지막에 `pause`하여 더블클릭 실행 시 결과 창이 즉시 닫히지 않습니다.
- 언어 리소스의 런타임 위치를 루트 `lang/`에서 `Data/lang/`으로 이동했습니다.
- 소스 템플릿도 `resources/Data/lang/`으로 통합하여 설치/빌드 배포 경로가 `Data/` 트리 하나로 일치합니다.

### 0.3.34 세로 탭 내비게이션 / lang 기반 다국어

- 원형 햄버거/접기 UI를 제거하고 왼쪽 고정 세로 탭(`설치 / AVD / 앱 / 로그`)으로 단순화했습니다.
- `resources/Data/lang/ko.lang`, `resources/Data/lang/en.lang`의 키-값 리소스로 UI 문자열을 분리했습니다.
- 설치 화면의 언어 설정에서 한국어/영어를 즉시 전환하며 `Data/Configs/settings.ini`의 `language=` 값으로 유지합니다.
- 번역 키가 현재 언어 파일에 없으면 영어 파일, 그 다음 키 이름 순서로 fallback합니다.
- 빌드 시 언어 파일을 실행 파일 옆 `Data/lang/`으로 자동 복사합니다.


### 0.3.13 Skia 빌드 진단

Windows Skia Ninja 빌드 출력은 `build/logs/skia-ninja.log`에 보존됩니다. 병렬 빌드가 실패하면 `-j1 -v`로 한 번 자동 재실행하여 실제 컴파일 명령과 진단을 `build/logs/skia-ninja-failure-verbose.log`에 저장하고 콘솔 마지막 160줄을 표시합니다.



### 0.3.15 Skia Expat bundled dependency fix

`is_official_build=true`에서 Expat도 시스템 라이브러리를 기본 사용하므로 Windows에 `expat.h`가 없으면 Skia XML 컴파일이 실패할 수 있습니다. Windows GN 설정에 `skia_use_system_expat=false`를 추가하여 `tools/git-sync-deps`가 내려받은 `third_party/externals/expat`를 사용하도록 고정했습니다. 기존 `build/` 폴더는 그대로 재사용할 수 있습니다.

### 0.3.14 Skia bundled dependency fix

`is_official_build=true`가 Windows에서 JPEG/PNG/WebP/zlib를 시스템 라이브러리로 찾는 기본값 때문에 `jpeglib.h`가 누락되던 문제를 수정했습니다. Windows GN 설정은 `skia_use_system_libjpeg_turbo=false`, `skia_use_system_libpng=false`, `skia_use_system_libwebp=false`, `skia_use_system_zlib=false`를 명시하여 `tools/git-sync-deps`가 내려받은 Skia 내부 의존성을 사용합니다. PortableAVM은 PDF 출력을 사용하지 않으므로 `skia_enable_pdf=false`도 적용합니다.


### 0.3.16 Windows source compatibility fix

- Fixed `Process.cpp` Windows build by including `<array>` and avoiding duplicate `WIN32_LEAN_AND_MEAN` definitions.
- Fixed Skia smart-pointer member declarations by including `SkRefCnt.h` in UI headers.
- Added the missing `SkPath.h` include.
- Replaced removed `SkFontMgr::RefDefault()` with the Windows DirectWrite font manager factory on Skia m126.
- Corrected Windows pipe reader handle lifetime so the reader thread finishes before its handle is closed.


### 0.3.17 Skia raster present compatibility fix

- Removed `SkSurface::flushAndSubmit()` from `SkiaRenderer::present()`. PortableAVM uses `SkSurfaces::Raster(...)`, so the frame is already CPU-backed and is copied to the SDL streaming texture through `peekPixels()`; there is no GPU submission step.
- Added source validation that rejects reintroducing `flushAndSubmit` into the raster renderer.
- Existing `build/` and the completed `skia.lib` are reusable.


### 0.3.21 Dynamic DPI

- PortableAVM 창을 `SDL_WINDOW_HIGH_PIXEL_DENSITY`로 생성하고 `SDL_GetWindowDisplayScale()`을 매 프레임 반영합니다.
- 모니터 이동이나 Windows 배율 변경 시 `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`/`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`에 즉시 대응합니다.
- Skia는 실제 픽셀 크기로 raster surface를 만들고 UI 좌표/마우스 입력은 logical content 좌표로 변환해 선명도와 클릭 위치를 함께 유지합니다.
- 실행 중 AVD는 `adb shell wm density <dpi>`로 재부팅 없이 DPI를 바꿀 수 있고 `wm density reset`으로 기본값을 복원할 수 있습니다.

### 0.3.21 Portable Android SDK isolation fix

- 정상적인 `Data/Runtime/Android/sdk/cmdline-tools/latest`가 이미 있으면 Command-line Tools를 다시 내려받아 덮어쓰지 않습니다.
- 손상된/부분 설치를 교체할 때는 임시 디렉터리에 먼저 완성한 뒤 `latest`로 이동하며, 기존 경로가 잠겨 있으면 명확한 오류를 표시합니다.
- `sdkmanager`, `avdmanager`, `adb`, `emulator` 자식 프로세스의 PATH에서 외부 Android SDK의 `cmdline-tools`, `platform-tools`, `emulator` 항목을 제거합니다.
- `ANDROID_HOME`, `ANDROID_SDK_ROOT`뿐 아니라 레거시 `ANDROID_SDK_HOME`도 data 아래로 고정합니다. NDK 변수는 SDK 관리 작업에서 비워 외부 Android Studio/SDK 설치와의 충돌을 줄입니다.
- 시스템 환경 변수 자체는 변경하지 않으며, 위 격리는 PortableAVM이 실행하는 자식 프로세스에만 적용됩니다.


### 0.3.22 수동 Command-line Tools 가져오기 / UI 정리 / 아이콘

- PortableAVM 내부의 Command-line Tools 자동 다운로드 기능을 제거했습니다. Android Developers의 Command-line tools only ZIP을 사용자가 직접 내려받은 뒤 설치 화면에서 ZIP을 선택해 가져옵니다.
- 앱 화면에서 Skia/SDL/빌드 버전 및 저작권·재배포 안내성 문구를 제거했습니다. 라이선스와 서드파티 고지는 소스/배포 문서에 계속 포함됩니다.
- `resources/PortableAVM.ico`가 Windows EXE 아이콘으로 빌드됩니다. 이 파일을 교체하고 다시 빌드하면 EXE 아이콘이 바뀝니다.
- `Data/Configs/PortableAVM.ico`가 있으면 시작 시 Windows 창/작업표시줄 아이콘으로 사용합니다. 해당 ICO를 바꾸고 앱을 다시 실행하면 빌드 없이 창 아이콘을 바꿀 수 있습니다.


### 0.3.23 Command-line Tools 가져오기 경로 자동 생성

- 수동 ZIP 가져오기 시작 전에 `data`, `temp`, `runtime/Android/sdk`, `cmdline-tools` 경로를 모두 자동 생성합니다.
- staging/prepared 경로의 부모 폴더도 존재 여부를 확인하고 없으면 생성합니다.
- ZIP 압축 해제 대상 폴더 생성 실패를 무시하지 않고 실제 경로와 운영체제 오류를 표시합니다.
- 따라서 완전히 비어 있는 `data`에서도 Command-line Tools ZIP 가져오기가 동작하도록 했습니다.


### 0.3.24 데이터 루트 이름 변경

- 런타임 데이터 루트 이름을 `Data/`로 통일했습니다.
- SDK, AVD, 설정, 로그, 임시 파일, 창 아이콘 override를 포함한 모든 포터블 경로가 `Data/`를 기준으로 동작합니다.
- Windows의 APPDATA/LOCALAPPDATA 격리 경로는 `Data/windows/roaming`, `Data/windows/local`을 사용합니다.

### 0.3.25 가져오기 안정화 / 접이식 에뮬레이터 사이드바

- Command-line Tools ZIP의 검증된 디렉터리는 `Data/temp`에서 SDK staging 위치로 우선 rename하고, rename이 불가능할 때만 파일별 복사를 사용합니다.
- 파일별 복사는 필요한 하위 폴더를 즉시 생성하고 실패한 원본/대상 경로를 오류에 포함합니다.
- 에뮬레이터 시작/종료 버튼을 메인 화면 최상단으로 이동했습니다. JDK, Command-line Tools, Emulator, ADB, 선택 system image, AVD가 모두 준비되기 전에는 시작 버튼이 비활성화됩니다.
- 버튼 라벨은 폭에 맞게 축소하고 버튼 경계로 clip하여 텍스트가 컨트롤 밖으로 그려지지 않습니다.


### 0.3.26 접이식 설정 사이드바

- 0.3.25에서 잘못 접던 에뮬레이터 패널 대신 왼쪽 설정 패널을 접고 펼칩니다.
- 설정 패널을 접으면 축소 메뉴 옆의 선택된 설정 페이지가 남은 폭을 모두 사용합니다.


### 0.3.27 설정 카드 메뉴 및 AVD 생성 안정화

- 설정 패널 토글을 원형 햄버거(세 줄) 버튼으로 변경했습니다.
- 햄버거 버튼 아래에 설치 / AVD / 앱 / 로그 설정 화면을 독립 카드로 분리했습니다.
- 생성 실패 시 반쪽짜리 AVD 파일을 정리하며, 성공 코드 뒤에도 `config.ini`가 실제 생성되었는지 확인합니다.


### 0.3.28 AVD 경로 판정 및 설정 탭/스크롤 개선

- Android 자식 프로세스에서는 최신 `ANDROID_HOME`, `ANDROID_USER_HOME`, `ANDROID_AVD_HOME`을 포터블 `Data` 아래로 고정하고 레거시 `ANDROID_SDK_HOME`은 상속하지 않습니다.
- AVD 생성은 `avdmanager create avd`의 `devices.xml`/SDK 위치 판정 오류를 피하기 위해 표준 `.ini`/`config.ini`를 `Data/AVD`에 직접 작성하며, 실행 시 선택한 시스템 이미지를 `-sysdir` 절대 경로로 고정합니다.
- 접힌 설정 패널은 `설치 / AVD / 앱 / 로그` 텍스트 메뉴만 보이고, 펼치면 서류 폴더 형태의 탭으로 설정 페이지를 전환합니다.
- 긴 설정 페이지에는 마우스 휠 스크롤 속도 개선과 드래그 가능한 스크롤바가 표시됩니다.

### 0.3.31 업로드 소스 정리 및 에뮬레이터 실행 안정화

- 사용자가 수정한 `Data` 구조, 수동 SDK 도구 import, 동적 DPI, 교체 가능한 아이콘, 접이식 설정 패널과 폴더형 탭은 유지했습니다.
- 접힌 설정 패널에는 `설치 / AVD / 앱 / 로그` 텍스트 메뉴가 남습니다. 펼친 상태에서는 시작/종료 및 탭은 고정되고 설정 내용만 독립적으로 스크롤됩니다.
- AVD 생성은 표준 descriptor/config 파일을 직접 작성하고 `image.sysdir.1`을 실제 system image 절대 경로로 저장합니다.
- 에뮬레이터 실행 시 `-sysdir`을 명시하고 시작 직후 종료 여부를 검사합니다. 즉시 종료하면 `Data/Logs/emulator-process.log`의 마지막 오류를 상태 메시지와 로그에 노출합니다.
- 이전 버전 설명과 validator의 상충 규칙을 현재 구현과 일치하도록 정리했습니다.

### 0.3.31 덮어쓰기 업그레이드 validator 보정

- `SOURCE_MANIFEST.sha256`이 있는 릴리스 트리에서는 금지 경로/placeholder 검사도 현재 패키지에 실제 포함된 경로만 검사합니다.
- 이전 버전에서 남은 `resources/data/` 또는 `.keep` 파일은 현재 manifest에 없으면 빌드를 막지 않습니다.
- 현재 manifest에 lowercase `resources/data` 또는 `.keep`가 다시 포함되면 여전히 검증 실패합니다.

### 0.3.32 Skia dependency sync cache/retry 개선

- Skia `DEPS` SHA-256 fingerprint와 준비된 bundled dependency를 확인해 증분 빌드에서는 `git-sync-deps`를 다시 실행하지 않습니다.
- 이전 버전에서 이미 `skia.lib`와 필요한 dependency가 완성된 build 캐시는 새 stamp를 자동 생성해 그대로 재사용합니다.
- 실제 dependency sync가 필요한 경우에는 전체 출력을 `build/logs/skia-git-sync-deps.log`에 기록하고 한 번 재시도합니다.
- upstream `git-sync-deps`의 병렬 checkout 중 하나가 실패해 `Thread failure detected`가 발생해도 재시도 후 로그 tail과 전체 로그 경로를 표시합니다.

### 0.3.33 Launcher UI / 상태바 / 라이선스 콘솔 개선

- PortableAVM은 Emulator 화면을 내장하지 않고, Android Emulator를 별도 네이티브 창으로 실행하는 런처로 동작합니다.
- 펼친 설정 화면은 상태바를 제외한 앱 전체 폭을 사용하고, 접힌 상태에서는 텍스트 메뉴 옆으로 선택된 설정 페이지가 확장됩니다.
- 작업 상태/오류 메시지는 창 맨 아래 고정 상태바에 표시되어 설정 페이지 스크롤과 분리됩니다.
- SDK 라이선스 터미널은 `cmd.exe /D /C`로 실행되어 `sdkmanager --licenses`가 끝나면 명령 프롬프트가 자동으로 닫힙니다.
- 이전 `WindowEmbedder`, `SetParent`, `embed_window` 설정 경로는 제거했습니다.

### 0.3.38 Emulator 창 표시 / 안전 종료 / 로그 선택 복사

- Android Emulator 런처의 콘솔은 `CREATE_NO_WINDOW`로 억제하고 Qt/QEMU GUI 창은 `SW_SHOW`/`SW_RESTORE`로 표시합니다.
- Emulator 로그 stdout/stderr 리다이렉션에는 상속 가능한 유효한 파일/NUL 핸들을 사용합니다.
- Job Object 안의 top-level Emulator 창을 찾아 표시/복원해 런처 모드에서 숨은 창 문제를 방지합니다.
- PortableAVM 종료 시 Emulator가 실행 중이면 확인합니다. 예를 선택해야 Emulator와 런처를 함께 종료하며, 아니오는 둘 다 계속 실행합니다.
- 런처가 비정상/강제 종료되면 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`가 Emulator 프로세스 트리를 함께 종료합니다.
- 로그 뷰에서 마우스 드래그 선택, Ctrl+A 전체 선택, Ctrl+C 복사가 가능합니다.

### 0.3.41 패키징 및 Data 최소화

- 소스 ZIP은 작업용 중간 경로를 포함하지 않고 `PortableAVM-0.3.41/` 하나를 최상위 폴더로 사용합니다.
- 기본 배포 `Data/`에는 런타임에 즉시 필요한 `lang/`만 포함합니다.
- `Logs`, `Locks`, `Configs`, `Runtime`, `AVD`, `temp` 등의 폴더는 해당 기능을 실제로 사용할 때만 생성합니다.
- 법적 문서와 개발 문서는 프로젝트 루트/`docs/`에만 유지하며 `Data/`에 중복 배포하지 않습니다.


### 0.3.42 빈 Data에서 JDK 가져오기 수정

- `Data/Runtime/jdk`가 아직 없는 새 설치에서도 JDK 가져오기가 실패하지 않도록 `std::filesystem::equivalent` 비교를 비예외 방식으로 변경했습니다.
- JDK 가져오기 시점에만 `Data/temp`와 `Data/Runtime`을 생성하고, 임시 복사/검증 후 `Data/Runtime/jdk`로 이동합니다.
- 완전히 빈 런타임 경로에서 가짜 JDK를 가져와 `jdkInstalled()`까지 확인하는 회귀 테스트를 추가했습니다.


### 0.3.43 직접 Emulator 실행 / 경로 사전 생성 / 레거시 정리

- `PortableAVM.EmulatorHost.exe`와 Host 전용 소스/CMake/검증 규칙을 완전히 제거했습니다.
- Windows에서는 PortableAVM 본체가 C++ `CreateProcessW()`로 `emulator.exe`를 직접 실행합니다.
- `CREATE_NO_WINDOW`는 emulator 런처의 콘솔만 억제하며, Job Object 안의 Qt/QEMU GUI 창은 별도로 찾아 표시합니다.
- Android 자식 프로세스가 사용하는 환경변수 경로는 실행 직전에 존재 여부를 확인하고 없으면 생성합니다.
- 앱 시작 시 불필요한 Data 하위 폴더를 모두 만드는 방식은 사용하지 않고, 기능별 사용 직전에 필요한 경로만 생성합니다.
- 프로젝트 및 `Data/` 폴더 역할은 `README.md`에 정리했습니다.

### 0.3.45 특정 실기기 기반 프리셋 제거

- 특정 실기기 화면/성능을 한 번에 적용하던 전용 프리셋 버튼과 함수를 제거했습니다.
- 기본 화면 구성을 일반 1080×2400 값으로 변경했습니다.
- 특정 실기기를 언급하던 한국어/영어 UI 문구와 문서 설명을 제거했습니다.
- RAM, CPU, 저장공간, 해상도, DPI, GPU, 공식 하드웨어 프로필 같은 일반 AVD 설정은 그대로 유지합니다.
