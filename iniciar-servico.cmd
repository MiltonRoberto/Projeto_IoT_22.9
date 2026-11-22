@echo off
if not exist "%~dp0.venv\Scripts\python.exe" (
  echo Ambiente Python ausente. Siga a instalacao no README.md.
  exit /b 1
)
"%~dp0.venv\Scripts\python.exe" "%~dp0servico\servico.py" --nome milton-amf-iot4-481337832642259969 --log "%~dp0servico\registros\wokwi.jsonl"
