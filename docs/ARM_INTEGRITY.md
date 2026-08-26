# ARM, 기기 프로필, Play Integrity의 경계

## 1. ARM 실행 지원

PortableAVM은 APK의 ZIP 중앙 디렉터리를 읽어 다음 경로를 찾습니다.

```text
lib/arm64-v8a/*.so
lib/armeabi-v7a/*.so
lib/x86_64/*.so
lib/x86/*.so
```

네이티브 라이브러리가 없으면 순수 Java/Kotlin 가능성을 표시합니다. 여러 ABI가 있으면 모두 표시하고, 현재 AVD 시스템 이미지 ABI와 직접 일치하는지 우선 판단합니다.

### ARM64 호스트

Windows on ARM64 또는 aarch64 Linux에서 Google이 공식 저장소로 제공하는 `arm64-v8a` 시스템 이미지를 선택하면 ARM64 게스트를 네이티브 CPU 계열 경로로 실행합니다. 실제 가속 가능 여부는 Android Emulator 패키지, OS hypervisor, CPU/펌웨어 설정에 달려 있습니다.

### x86-64 호스트

x86-64 CPU에서 ARM64 시스템 이미지는 ARM 네이티브 실행이 아닙니다. PortableAVM은 호스트/게스트 ABI 불일치를 감지해 가속을 끄고 큰 성능 경고를 표시합니다. 공식 Emulator가 그 조합을 거부하거나 앱이 필요한 변환 경로를 제공하지 않으면 실행되지 않습니다.

### 포함하지 않는 것

- 다른 상용 Emulator에서 추출한 ARM 변환 라이브러리
- Intel Houdini 또는 출처·재배포 권한이 불명확한 바이너리
- 수정된 system/vendor image
- 앱 ABI를 속이는 후킹
- 앱·게임 APK/OBB

이 범위는 저작권, 재배포 조건, 공급망 보안 문제를 피하고 “지원”과 “우회”를 구분하기 위한 것입니다.

## 2. 하드웨어 프리셋과 기기 신원은 다름

해상도, DPI, RAM, CPU 코어 수 같은 화면·자원 설정은 일반적인 AVD 하드웨어 구성입니다. 반면 아래 값은 앱이 보는 인증된 기기 신원 또는 무결성 판단과 관련될 수 있습니다.

```text
ro.build.fingerprint
ro.product.model
ro.product.brand
ro.product.manufacturer
verified boot / vbmeta 관련 속성
attestation key 또는 인증서
```

PortableAVM은 위 속성의 사용자 덮어쓰기를 차단합니다. 특정 실제 휴대폰으로 가장하는 이름을 UI에 표시하더라도 하드웨어 기반 키, Google 인증 상태, OEM 서명 체인이 생기지 않으므로 Play Integrity를 정상적으로 획득하는 방법이 아닙니다.

## 3. BlueStacks와 Play Integrity

BlueStacks의 공식 도움말에는 앱 호환성을 위해 인스턴스의 “device profile”을 바꾸는 기능이 설명되어 있습니다. 이것은 앱에 노출되는 모델 프로필을 바꾸는 호환성 기능이지, 모든 앱·모든 인스턴스에서 특정 Play Integrity verdict를 보장한다는 뜻은 아닙니다.

Play Integrity 결과는 앱이 요청하는 verdict, Google Play 인식 상태, OS/부팅 상태, 앱 설치·라이선스 상태, 환경 위험 신호 등에 따라 달라질 수 있습니다. 상용 Emulator가 어느 시점의 일부 구성에서 일부 판정을 통과하더라도, 앱 업데이트나 Emulator 업데이트 뒤에도 계속 통과한다는 계약상·기술상 보장은 아닙니다.

PortableAVM은 다음을 구현하지 않습니다.

- Play Integrity API 응답 위조 또는 프록시
- 하드웨어 attestation 키·인증서 복제
- Google/OEM 인증 상태 가장
- 빌드 지문·verified boot 판정 조작
- 앱의 DRM·Emulator 탐지·안티치트 패치

## 4. 게임 호환성을 높이는 허용 가능한 방법

- 게임이 지원하는 Android API와 ABI를 확인하고 일치하는 공식 시스템 이미지를 선택합니다.
- ARM64 호스트에서는 공식 ARM64 이미지를, x86-64 호스트에서는 가능하면 x86_64 네이티브 라이브러리를 포함한 앱을 사용합니다.
- 최신 공식 Emulator·platform-tools·GPU 드라이버와 OS hypervisor를 사용합니다.
- 앱 배포자가 Emulator를 허용하는지 서비스 약관과 지원 문서를 확인합니다.
- Google Play가 필요한 앱은 Google Play 시스템 이미지와 정식 계정·정식 배포 경로를 사용하되, 해당 앱의 무결성 정책이 Emulator를 허용하지 않으면 그 결정을 우회하지 않습니다.

## 참고 문서

- Android NDK ABI: https://developer.android.com/ndk/guides/abis
- Android Emulator 가속: https://developer.android.com/studio/run/emulator-acceleration
- Play Integrity 개요: https://developer.android.com/google/play/integrity/overview
- BlueStacks 5 device profile 도움말: https://support.bluestacks.com/hc/en-us/articles/360057788651-How-to-switch-device-profile-in-BlueStacks-5
