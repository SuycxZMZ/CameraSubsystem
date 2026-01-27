#!/bin/bash

# CameraSubsystem 构建脚本
# 用途: 编译项目并运行测试

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  CameraSubsystem 构建脚本${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 进入项目根目录
cd "${PROJECT_ROOT}"

# 创建构建目录
if [ ! -d "${BUILD_DIR}" ]; then
    echo -e "${YELLOW}创建构建目录...${NC}"
    mkdir -p "${BUILD_DIR}"
fi

# 进入构建目录
cd "${BUILD_DIR}"

# 配置 CMake
echo -e "${YELLOW}配置 CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 编译项目
echo -e "${YELLOW}编译项目...${NC}"
make -j$(nproc)

# 运行测试
echo -e "${YELLOW}运行测试...${NC}"
ctest --output-on-failure

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  构建完成！${NC}"
echo -e "${GREEN}========================================${NC}"
