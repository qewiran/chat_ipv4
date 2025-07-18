#! /bin/bash

if command -v ninja >/dev/null 2>&1; then
  build_system="Ninja"
  build_system_bin="$(command -v ninja)"
else 
  build_system="Unix Makefiles"
  build_system_bin="$(command -v make)"
fi

echo "▸ Система сборки: ${build_system}"
echo "▸ Путь к бинарнику системы: ${build_system_bin}"

temp_build_dir=$(mktemp -d "$(pwd)/build.XXXXXX")

cleanup() {
    echo "▸ Очистка файлов сборки..."
    if [[ -d "${temp_build_dir}" ]]; then
        rm -rf "${temp_build_dir}"
    fi
}

trap cleanup EXIT

{
  cd "${temp_build_dir}" || exit 1  
  cmake  -G "${build_system}" "$@" .. || { echo "CMake fail"; rm -rf ${temp_build_dir}; exit 1; }
  echo "▸ Сборка..."
  "${build_system_bin}" || { echo "Build fail"; rm -rf ${temp_build_dir}; exit 1; }
  mv chat .. 2>/dev/null
}


echo "▸ Успех! Бинарник чата называется chat, использование ./chat <ip> <port>."
