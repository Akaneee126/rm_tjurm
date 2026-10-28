#!/bin/bash

# TJURM-2024 启动脚本
#   - Nvidia 平台：推理走 TensorRT（OpenRM 自动启用）
#   - AidLux 犀牛派 A1/X1：推理走 AidLite + Qualcomm QNN(NPU)
#
# 可选参数：
#   -r            删除 build 后重新编译
#   -c            用 data/uniconfig 覆盖 /etc/openrm
#   -l            先编译安装 OpenRM
#   -s            开启 imshow（虚拟显示器或远程桌面下使用）
#   -b            只编译，不运行（首次部署自检推荐）
#   -g "message"  git 提交

blue="\033[1;34m"
yellow="\033[1;33m"
reset="\033[0m"

include_count=$(find include  -type f \( -name "*.cpp" -o -name "*.h" \) -exec cat {} \; | wc -l)
src_count=$(find src  -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.txt" \) -exec cat {} \; | wc -l)
total=$((include_count + src_count))

if [ ! -d "data/debug" ]; then
    mkdir -p data/debug
    touch data/debug/here_save_debug_images
fi

if [ ! -d "data/video" ]; then
    mkdir -p data/video
    touch data/video/here_save_video
fi

if [ ! -d "data/speed" ]; then
    mkdir -p data/speed
    touch data/speed/here_save_shoot_speed
fi

if [ ! -d "/etc/openrm" ]; then 
    sudo mkdir -p /etc/openrm
    sudo cp -r data/uniconfig/* /etc/openrm/
    sudo chmod -R 777 /etc/openrm
fi

# 录像 / debug 目录（Config.json 里默认指向这里，避免写到不存在的主目录）
sudo mkdir -p /etc/openrm/video /etc/openrm/debug
sudo chmod -R 777 /etc/openrm/video /etc/openrm/debug

if [ ! -d "config" ]; then 
    ln -s /etc/openrm ./config
fi


if [ ! -d "build" ]; then 
    mkdir build
fi

imshow=0
build_only=0

while getopts ":rcg:lsb" opt; do
    case $opt in
        r)
            echo -e "${yellow}<--- delete 'build' --->\n${reset}"
            sudo rm -rf build
            mkdir build
            shift
            ;;

        c)
            sudo cp -r data/uniconfig/* /etc/openrm/
            sudo chmod -R 777 /etc/openrm
            exit 0
            shift
            ;;
        g)
            git_message=$OPTARG
            echo -e "${yellow}\n<--- Git $git_message --->${reset}"
            git pull
            git add -A
            git commit -m "$git_message"
            git push
            exit 0
            shift
            ;;

        l)
            cd ../OpenRM
            sudo ./run.sh
            cd ../TJURM-2024
            exit 0
            shift
            ;;
        s)
            imshow=1
            shift
            ;;
        b)
            build_only=1
            shift
            ;;
        \?)
            echo -e "${yellow}\n--- Unavailable param: -$OPTARG ---\n${reset}"
            ;;
        :)
            echo -e "${yellow}\n--- param -$OPTARG need a value ---\n${reset}"
            ;;
        esac
    done


echo -e "${yellow}<--- Start CMake --->${reset}"
cd build
cmake ..

if [ $? -ne 0 ]; then
    echo -e "${yellow}<--- CMake failed, check OpenRM install & OpenCV --->${reset}"
    exit 1
fi


echo -e "${yellow}\n<--- Start Make --->${reset}"
max_threads=$(cat /proc/cpuinfo | grep "processor" | wc -l)
make -j "$max_threads"

if [ $? -ne 0 ]; then
    echo -e "${yellow}<--- Make failed --->${reset}"
    exit 1
fi


echo -e "${yellow}\n<--- Total Lines --->${reset}"
echo -e "${blue}        $total${reset}"

if [ $build_only -eq 1 ]; then
    echo -e "${yellow}\n<--- Build done (skip run) --->${reset}"
    exit 0
fi

echo -e "${yellow}\n<--- Run Code --->${reset}"
sudo rm -f /usr/local/bin/TJURM-2024
sudo cp TJURM-2024 /usr/local/bin/
sudo pkill TJURM-2024
sudo chmod 777 /dev/tty* 2>/dev/null

if [ $imshow = 1 ]; then
    TJURM-2024 -s
else
    TJURM-2024

fi

# guard.sh 里包含 nvpmodel / jetson_clocks，非 Nvidia 平台会直接跳过
if [ -x /etc/openrm/guard.sh ]; then
    /etc/openrm/guard.sh
fi

echo -e "${yellow}<----- OVER ----->${reset}"
