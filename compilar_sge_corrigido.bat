@echo off
setlocal
cd /d "%~dp0"
title Compilar SGE - Login

echo ==============================================
echo   Compilacao do SGE - Login Real
echo ==============================================
echo.

set "MSYS_UCRT64=C:\msys64\ucrt64"
set "MSYS_ROOT=C:\msys64"
set "PATH=%MSYS_UCRT64%\bin;%MSYS_ROOT%\usr\bin;%PATH%"
set "GCC=%MSYS_UCRT64%\bin\gcc.exe"
if not exist "%GCC%" (
  echo ERRO: GCC do MSYS2 UCRT64 nao encontrado em:
  echo %GCC%
  echo.
  echo Confira se o MSYS2 UCRT64 esta instalado nesse caminho.
  pause
  exit /b 2
)

if not exist "backend\src\server.c" (
  echo ERRO: backend\src\server.c nao encontrado.
  echo Salve este arquivo na raiz da pasta PIM_2, junto das pastas backend e frontend.
  pause
  exit /b 3
)
if not exist "backend\third_party\mongoose\mongoose.c" (
  echo ERRO: arquivo mongoose.c nao encontrado em backend\third_party\mongoose.
  pause
  exit /b 4
)
if not exist "build" mkdir "build"
if exist "build\sge_server.exe" del /f /q "build\sge_server.exe"

echo Pasta atual: %CD%
echo Compilador: %GCC%
echo.
echo PATH do MSYS2 configurado para localizar as DLLs do compilador.
echo Executando compilacao. Aguarde...
echo.

"%GCC%" -D_POSIX_C_SOURCE=200809L -D_WIN32_WINNT=0x0601 "backend\src\server.c" "backend\third_party\mongoose\mongoose.c" -I"backend\third_party\mongoose" -o "build\sge_server.exe" -lws2_32 -lodbc32 -lbcrypt
set "BUILD_RESULT=%ERRORLEVEL%"
echo.
echo ==============================================
echo Codigo de saida: %BUILD_RESULT%
if "%BUILD_RESULT%"=="0" (
  if exist "build\sge_server.exe" (
    echo SUCESSO: executavel criado em build\sge_server.exe
    echo Para iniciar, execute: build\sge_server.exe
  ) else (
    echo ERRO: GCC retornou sucesso, mas o executavel nao foi encontrado.
  )
) else (
  echo FALHA: compilacao nao concluida. Copie uma foto desta janela mostrando o erro acima.
)
echo ==============================================
echo.
pause
exit /b %BUILD_RESULT%
