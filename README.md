# AliceRenderer
DirectX 11 기반 3D 게임 엔진

<img width="2002" height="1165" alt="image" src="https://github.com/user-attachments/assets/9dea4cd7-dee2-47c6-bca4-e2ed3411c6a6" />

<img width="2002" height="1165" alt="image" src="https://github.com/user-attachments/assets/96aa2ebd-ce62-415b-9a54-1d8cec722aec" />

<img width="2002" height="1165" alt="image" src="https://github.com/user-attachments/assets/d983fac4-5eb0-470a-a8f8-6d7e09114e4d" />


- 엔진 구조
<img width="2961" height="857" alt="다이어그램" src="https://github.com/user-attachments/assets/53ca1e2a-85f8-4628-a621-f424545b0f2c" />


- 빌드 과정
  - Setup.bat 파일을 실행해서 vcpkg, assimp등 의존성을 전부 다운로드하세요. (Setup.bat 파일 내부에 경로 설정이 가능합니다. 디폴트는 D:\vcpkg)
  - build_msvc.cmd 파일을 실행해서 솔루션 빌드 파일을 만드세요. (build 폴더 내부에 폴더가 생깁니다)
  - build 폴더안에 솔루션 파일을 실행해서 빌드하세요
  - Launch 프로젝트를 시작 프로그램으로 설정하면 됩니다.
    
    <img width="543" height="516" alt="image" src="https://github.com/user-attachments/assets/4bed66c5-44c4-419e-927d-5863f0a917a9" />
    
  - 경로에 한글이 없어야 합니다
