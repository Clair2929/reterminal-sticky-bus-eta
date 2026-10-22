# ============================================================
#  v54.ps1 — 一键切换到 ESP-IDF v5.4 编译环境
#  用法：在 PowerShell 中运行  .\v54.ps1
#        然后就可以直接  idf.py build / idf.py -p COM25 flash
#  注意：只在【当前窗口】生效；新开窗口需重新运行本脚本。
#  ============================================================
#  铁律：本项目触摸（GT911）只在 v5.4 下正常。
#        绝不要用 5.4.4 的 export.bat / idf.py build！
# ============================================================

$env:IDF_PATH          = 'D:\Espressif\frameworks\esp-idf-v5.4'
$env:IDF_TOOLS_PATH    = 'D:\Espressif'
$env:IDF_PYTHON_ENV_PATH = 'D:\Espressif\python_env\idf5.4_py3.11_env'

$tools = @(
    'D:\Espressif\python_env\idf5.4_py3.11_env\Scripts'
    'D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin'
    'D:\Espressif\tools\cmake\3.30.2\bin'
    'D:\Espressif\tools\ninja\1.12.1'
    'D:\Espressif\tools\idf-git\2.44.0\cmd'
) -join ';'
$env:PATH = "$tools;$env:PATH"

Write-Host ''
Write-Host '== 已切换到 ESP-IDF v5.4 环境 ==' -ForegroundColor Green
Write-Host ("  IDF_PATH       = " + $env:IDF_PATH)
Write-Host ("  TOOLCHAIN      = esp-14.2.0_20241119")

# 打印版本做双重确认（必须是 v5.4，出现 5.4.4 就是环境没切对）
& 'D:\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe' `
   'D:\Espressif\frameworks\esp-idf-v5.4\tools\idf.py' --version

if ($LASTEXITCODE -ne 0) { exit 1 }
Write-Host ''
Write-Host '接下来：idf.py build   或   idf.py -p COM25 flash' -ForegroundColor Yellow
