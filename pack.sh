#!/bin/bash
# 一键打包 desk-quant 到 DeskQuant_SendOut 文件夹
# 用法：cd /c/DeskQuant && bash pack.sh
set -e

cd /c/DeskQuant

echo "==> 1/6 清空输出目录"
rm -rf DeskQuant_SendOut
mkdir -p DeskQuant_SendOut/data

echo "==> 2/6 复制主程序"
cp build/deskquant.exe DeskQuant_SendOut/

echo "==> 3/6 部署 Qt 依赖（windeployqt）"
if command -v windeployqt >/dev/null 2>&1; then
    windeployqt DeskQuant_SendOut/deskquant.exe >/dev/null
else
    /c/msys64/ucrt64/bin/windeployqt.exe DeskQuant_SendOut/deskquant.exe >/dev/null
fi

echo "==> 4/6 递归复制 MSYS2 运行时 DLL（含间接依赖）"
cd DeskQuant_SendOut
for i in 1 2 3 4 5 6 7 8 9 10; do
    for f in *.exe *.dll; do
        [ -f "$f" ] || continue
        objdump -p "$f" 2>/dev/null | grep "DLL Name" | awk '{print $3}' | while read -r dll; do
            if [ -f "/c/msys64/ucrt64/bin/$dll" ] && [ ! -f "./$dll" ]; then
                cp "/c/msys64/ucrt64/bin/$dll" .
            fi
        done
    done
done
cd ..

# 显式复制 Python 运行时 DLL（pybind11 嵌入需要，objdump 可能因延迟加载漏掉）
cp /c/msys64/ucrt64/bin/*python*.dll DeskQuant_SendOut/ 2>/dev/null || echo "    ⚠ 未找到 Python DLL"

echo "==> 5/6 复制 Python 标准库 + 数据 + README"
mkdir -p DeskQuant_SendOut/lib
cp -r /c/msys64/ucrt64/lib/python3.* DeskQuant_SendOut/lib/ 2>/dev/null || echo "    ⚠ 无 Python 标准库（Python 策略功能无法在别人电脑用）"
cp data/*.csv DeskQuant_SendOut/data/ 2>/dev/null || echo "    无数据文件"
cp README.md DeskQuant_SendOut/ 2>/dev/null || true

echo "==> 6/6 完成"
echo "✅ 打包完成：DeskQuant_SendOut/"
echo "   压缩成 zip 发给同学即可（双击 deskquant.exe 运行）"
echo "   ⚠ Python 策略功能跨电脑运行需设 PYTHONHOME（见 README）"
