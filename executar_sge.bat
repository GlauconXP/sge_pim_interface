@echo off
setlocal
cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
if not exist ".\build\sge_server.exe" (
  echo ERRO: build\sge_server.exe nao existe.
  echo Execute compilar_sge_corrigido.bat primeiro.
  pause
  exit /b 1
)
echo Iniciando o servidor SGE...
echo Deixe esta janela aberta enquanto usa o sistema.
echo Para encerrar o servidor, pressione Ctrl+C.
echo.
".\build\sge_server.exe"
echo.
echo O servidor foi encerrado.
pause
