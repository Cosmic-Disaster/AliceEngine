@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ----------------------------------------------------------------------
REM [창 제어 설정]
REM /c : 스크립트 실행이 끝나면 창을 닫습니다. (단, 마지막 pause 대기 후)
REM ----------------------------------------------------------------------
if /i "%~1" neq "__INVOKED" (
    cmd /c ""%~f0" __INVOKED"
    exit /b
)

echo ========================================================
echo [Build.bat] Engine 스마트 빌드 시스템
echo 목표: 완전 자동화 (중간 멈춤 없음 -> 결과 확인 후 종료)
echo ========================================================

set "EXIT_CODE=0"
set "ENGINE_DIR=%~dp0Engine"

REM -----------------------------------------------------------
REM 1. Git 설치 및 폴더 확인
REM -----------------------------------------------------------
where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [FAIL] Git이 없습니다.
    set "EXIT_CODE=1"
    goto :End
)

if not exist "%ENGINE_DIR%\.git" (
    echo [INFO] Engine 폴더가 비어있어 초기화를 진행합니다.
    git submodule update --init --recursive
)

REM -----------------------------------------------------------
REM 2. Engine 변경사항 감지 (Dirty Check)
REM -----------------------------------------------------------
echo.
echo [STEP 1] Engine 로컬 변경사항 확인 중...
pushd "%ENGINE_DIR%"

set "IS_DIRTY=0"
git diff --quiet
if errorlevel 1 set "IS_DIRTY=1"
git diff --cached --quiet
if errorlevel 1 set "IS_DIRTY=1"
for /f "delims=" %%F in ('git ls-files --others --exclude-standard') do set "IS_DIRTY=1"
popd

REM -----------------------------------------------------------
REM 3. 분기 처리
REM -----------------------------------------------------------
if "%IS_DIRTY%"=="0" (
    echo [INFO] 변경사항이 없습니다. 최신 버전 동기화를 진행합니다.
    goto :DoUpdate
)

echo.
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo [WARN] Engine 내부에 수정된 코드나 새로운 파일이 있습니다!
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo.
echo [Y] : 내 변경사항 삭제(Reset) 후 최신 버전 빌드 (권장)
echo [N] : 현재 내가 수정한 코드 그대로 빌드
echo.
set /p USER_CHOICE="선택하세요 (Y/N): "

if /i "!USER_CHOICE!"=="Y" goto :DoUpdate
if /i "!USER_CHOICE!"=="N" goto :SkipUpdate

echo [INFO] 유효하지 않은 입력입니다. 현재 코드로 진행합니다.
goto :SkipUpdate


REM -----------------------------------------------------------
REM [루틴 A] 강제 업데이트
REM -----------------------------------------------------------
:DoUpdate
echo.
echo [STEP 2] Engine을 Stable 최신으로 강제 동기화합니다...
pushd "%ENGINE_DIR%"

git fetch origin
if errorlevel 1 (
    echo [FAIL] Git Fetch 실패. 인터넷 상태를 확인하세요.
    popd
    set "EXIT_CODE=1"
    goto :End
)

git checkout -B stable origin/stable
git reset --hard origin/stable
git clean -fd
git submodule update --init --recursive

popd
echo [OK] 최신 버전 동기화 완료.
goto :BuildSequence


REM -----------------------------------------------------------
REM [루틴 B] 업데이트 건너뛰기
REM -----------------------------------------------------------
:SkipUpdate
echo.
echo [STEP 2] Git 업데이트를 건너뛰고 현재 상태로 빌드합니다.
goto :BuildSequence


REM -----------------------------------------------------------
REM 4. Setup 및 CMake 빌드 (중간 멈춤 방지 적용)
REM -----------------------------------------------------------
:BuildSequence
echo.
echo [STEP 3] Engine Setup (라이브러리 설정)
pushd "%ENGINE_DIR%"

if exist "Setup.bat" (
    REM [핵심] echo. | call ... 
    REM 내부 pause를 스킵하여 매끄럽게 진행
    echo. | call Setup.bat
    if errorlevel 1 (
        echo [FAIL] Setup.bat 실행 실패
        popd
        set "EXIT_CODE=1"
        goto :End
    )
) else (
    echo [FAIL] Setup.bat 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo [STEP 4] 솔루션 생성 (build_msvc.cmd)
if exist "build_msvc.cmd" (
    REM [핵심] echo. | call ...
    echo. | call build_msvc.cmd
    if errorlevel 1 (
        echo [FAIL] build_msvc.cmd 실행 실패
        popd
        set "EXIT_CODE=1"
        goto :End
    )
) else (
    echo [FAIL] build_msvc.cmd 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)

popd

echo.
echo ========================================================
echo [SUCCESS] 모든 작업이 완료되었습니다.
echo ========================================================

:End
echo.
if "%EXIT_CODE%"=="0" (
    echo [RESULT] SUCCESS
) else (
    echo [RESULT] FAIL ^(ExitCode=%EXIT_CODE%^)
)

echo.
echo (아무 키나 누르면 창이 닫힙니다.)
pause
exit /b %EXIT_CODE%