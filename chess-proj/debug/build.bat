@echo off

:: 1. 运行 Python 脚本
python chesskiller.py

:: 2. 设置 JSON 文件路径变量 (注意 Windows 使用反斜杠 \)
set "cpp_json=..\outcome\chess_killer\checkpoint_cpp_test.json"

:: 3. 如果文件存在则删除 (-f 变为 if exist, rm 变为 del)
if exist "%cpp_json%" (
    del "%cpp_json%"
)

:: 4. 编译 C++ 代码
:: 注意：Windows 下生成的可执行文件通常会自动带上 .exe 后缀
g++ --std=c++17 chesskiller_d.cpp -o chesskiller.exe

:: 5. 检查编译是否成功，成功则运行程序
if %errorlevel% equ 0 (
    chesskiller.exe
) else (
    echo Compilation failed.
    pause
    exit /b
)

:: 6. 输出完成
echo done
pause