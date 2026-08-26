# 빌드 상세

## 지원 대상

- 주 대상: Windows 10/11 x64 또는 Windows on ARM64
- 보조 대상: 최신 x86_64/aarch64 Linux
- 언어/빌드: C++20, CMake 3.24+, Ninja
- GUI: Skia CPU raster + SDL3
- 네트워크: libcurl. Windows 번들은 Schannel을 사용합니다.

## 다운로드되는 항목

`build-windows.bat`은 Windows에서 `build/deps/` 아래에, `build-linux.sh`는 Linux에서 `build/deps/` 아래에 다음 저장소를 체크아웃합니다.

| 구성요소 | 공식 저장소 | 기본 ref | 용도 |
|---|---|---|---|
| depot_tools | chromium.googlesource.com/chromium/tools/depot_tools.git | main | GN/Ninja 도구 |
| Skia | skia.googlesource.com/skia.git | chrome/m126 | CPU 래스터 UI |
| SDL3 | github.com/libsdl-org/SDL.git | release-3.2.0 | 창·입력·클립보드 |
| curl | github.com/curl/curl.git | curl-8_11_1 | Windows HTTPS |

기본 ref는 재현 가능한 API 조합을 위한 기준입니다. 환경 변수 `PAVM_SKIA_REF`, `PAVM_SDL_REF`, `PAVM_CURL_REF`로 바꿀 수 있지만, API 변화에 따른 소스 수정과 별도 검증이 필요합니다.

## Windows

```bat
build-windows.bat -Clean
```

Git, CMake, Python은 시스템에 설치하지 않습니다. `install-prerequisites-windows.bat`가 공식 배포본을 `build/tools/` 아래에 내려받고 `build/tools/env.cmd`를 생성합니다. 이 환경 파일을 `call`한 현재 `cmd.exe` 또는 이를 호출한 빌드 스크립트에서만 PATH/PYTHONHOME가 바뀌며 사용자/시스템 환경변수는 수정하지 않습니다.

Visual Studio는 `vswhere.exe`로 **어떤 에디션이든 Visual Studio 2026 (18.x) 설치 인스턴스가 존재하는지만** 확인합니다. Desktop C++ workload나 특정 MSVC 구성요소를 탐색 조건으로 요구하지 않습니다. VS 2026이 하나라도 있으면 그 설치를 그대로 선택하고 `Common7\Tools\VsDevCmd.bat`로 환경을 초기화하여 그 인스턴스가 선택하는 최신 설치 toolset을 사용합니다. MSVC/`cl.exe`의 세부 버전 번호는 하드코딩하지 않습니다. VS 2026 자체가 없을 때만 Microsoft의 VS 18 stable Build Tools bootstrapper를 내려받아 `Microsoft.VisualStudio.Workload.VCTools`를 설치합니다. 기존 VS 2026은 자동으로 다른 인스턴스로 대체하거나 재설치하지 않습니다.

출력:

```text
dist-windows-x64/
├─ PortableAVM.exe
├─ Data/...
├─ docs/...
├─ licenses/
│  ├─ Skia-LICENSE.txt
│  ├─ SDL3-LICENSE.txt
│  └─ curl-COPYING.txt
```

`-SkipDependencyBuild`는 동일 ref와 아키텍처로 Skia가 이미 만들어진 경우에만 사용합니다.

## Linux

```bash
./scripts/build-linux.sh --install-deps --clean
```

`--install-deps`는 apt, dnf, pacman을 감지해 컴파일러, CMake/Ninja, libcurl, Fontconfig/FreeType, SDL의 Linux 백엔드 개발 패키지를 설치합니다. 관리 대상 시스템에서는 목록을 검토한 뒤 수동 설치하는 편이 안전합니다.

## Android 런타임은 빌드 의존성이 아님

Android Command-line Tools, Emulator, platform-tools, system images, JDK는 PortableAVM 프로그램을 컴파일할 때 내려받지 않습니다. 완성된 앱을 실행한 사용자가 고지와 라이선스를 확인한 뒤 공식 Android 저장소에서 별도로 설치합니다.

## 전체 검증 범위

1. `validate-source.py`: 번들·Qt·영구 환경변수·자동 라이선스 수락 정책 검사
2. C++ 코어 컴파일과 `pavm_core_tests`
3. Skia/SDL3 실제 헤더를 사용한 전체 translation-unit 문법 검사
4. 대상 OS에서 제3자 의존성 빌드와 최종 링크
5. 설치 결과의 Qt DLL/so 이름 검사와 라이선스·빌드 manifest 생성
6. 실기기 Windows에서 SDK 다운로드, AVD 생성/부팅, GPU/Hypervisor, 별도 Emulator 창 실행, 종료 정리 테스트

이 저장소를 만든 환경에서 1~3은 자동 검사할 수 있지만, Windows MSVC 최종 링크와 Android Emulator/게임 실행은 Windows 호스트에서 수행해야 합니다.
