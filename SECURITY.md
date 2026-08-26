# Security

## 다운로드

- Android SDK bootstrap metadata와 archive는 HTTPS만 허용합니다.
- Command-line Tools archive는 Google Android repository host allow-list, XML의 크기, SHA 체크섬을 모두 검사합니다.
- archive 경로를 추출 전에 검증하고 `Data/temp` staging을 거칩니다.
- 빌드 의존성은 명시된 공식 Git 저장소와 ref에서 체크아웃하며 결과물에 커밋을 기록합니다.

## 비밀 정보

ADB 키, SDK 설정, 사용자 home, 로그는 `data` 아래에 생성됩니다. 이 폴더에는 계정 토큰, 앱 데이터, ADB 개인키가 포함될 수 있으므로 공개 업로드하거나 다른 사용자와 공유하지 마십시오.

## 취약점 보고

공개 배포판에서는 저장소의 보안 연락처를 별도로 지정하십시오. 보고에는 PortableAVM 버전, OS/아키텍처, 재현 단계, 관련 로그 중 비밀 정보를 제거한 사본을 포함하는 것이 좋습니다.
