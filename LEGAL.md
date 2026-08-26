# 법률·배포 유의사항

이 문서는 일반적인 프로젝트 설계 메모이며 변호사의 법률 의견이나 특정 국가에서의 적법성 보증이 아닙니다.

## Android SDK와 Google 구성요소

PortableAVM 소스와 기본 배포물에는 Android SDK, Emulator, platform-tools, system image, Google APIs/Google Play image, Command-line Tools archive를 포함하지 않습니다. 사용자가 공식 저장소에서 직접 받도록 하며, 다운로드 전 Google의 현재 약관·개인정보 문서 링크를 표시하고 명시적인 동의를 요구합니다. `sdkmanager --licenses` 입력을 자동으로 전부 승인하지 않습니다.

Android SDK 또는 Google Play 구성요소를 PortableAVM과 함께 재배포하려면 각 패키지의 현재 라이선스가 허용하는지 별도로 검토해야 합니다. 이 프로젝트의 기본 패키징 흐름은 이를 금지합니다.

## JDK

JDK는 번들하지 않습니다. 사용자가 적법하게 확보한 JDK 디렉터리를 포터블 런타임 폴더로 복사합니다. 실제 배포 시 선택한 JDK 공급자의 라이선스와 고지 의무를 따라야 합니다.

## 게임과 앱

APK, AAB, OBB, 계정 데이터, 게임 리소스는 제공하지 않습니다. 사용자는 정식 배포 경로에서 합법적으로 확보한 앱만 설치하고, 앱·게임 서비스 약관, 지역 제한, 계정 정책, 자동화/다중 실행/Emulator 제한을 준수해야 합니다.

## 무결성·DRM·안티치트

PortableAVM은 Play Integrity 응답, 기기 attestation, OEM/Google 인증, verified boot, DRM, Emulator 탐지, 루팅 탐지 또는 안티치트를 우회하지 않습니다. 실제 기기 모델·브랜드·빌드 지문·인증키를 위조하는 기능을 제공하지 않습니다. 하드웨어 프리셋은 해상도·DPI·RAM·CPU·저장공간·GPU처럼 일반 AVD 설정만 바꿉니다.

## ARM

공식 Android 저장소의 ARM64 시스템 이미지와 앱에 포함된 ABI를 지원합니다. 출처 또는 재배포 권한이 불명확한 ARM 변환 계층, 다른 Emulator에서 추출한 바이너리, 수정 system/vendor image는 포함하지 않습니다.

## 상표

Android는 Google LLC의 상표일 수 있으며, Galaxy는 Samsung Electronics의 상표일 수 있고, BlueStacks는 해당 권리자의 상표입니다. 이름을 호환성 설명에 사용할 때 공식 제휴·후원·인증을 암시해서는 안 됩니다. 제품명·아이콘·마케팅 문구는 배포 국가의 상표법과 브랜드 가이드에 맞게 검토하십시오.

## PortableAVM와 제3자 오픈소스

PortableAVM 자체 코드는 MIT 라이선스입니다. Skia, SDL3, curl 및 그 전이 의존성에는 각각 별도 조건이 적용됩니다. 빌드 스크립트는 checkout의 라이선스 파일을 결과물에 복사하지만, 배포자는 사용한 정확한 커밋과 빌드 옵션에 따른 전체 고지 의무를 확인해야 합니다.
