Сборка проекта BananaInjector на Windows (Visual Studio 2022 / 2019)

Требуется:
- Visual Studio 2019/2022 с "Desktop development with C++"
- Windows 10/11 SDK (обычно устанавливается с VS)

Инструкция (рекомендуется):
1) Откройте "x64 Native Tools Command Prompt for VS 2022".
2) В директории проекта (где находится CMakeLists.txt) выполните:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

3) В артефактах сборки файл `bananainjector.exe` будет в `build/Release/`.

Примечания:
- Проект уже включает `MinHook` и `imgui` (в папке `MinHook` и `imgui`).
- Если хотите статическую CRT (/MT), откройте `build` -> откройте `.sln` в Visual Studio и убедитесь, что в свойствах проекта для конфигурации `Release|x64` используется `Multi-threaded` (C/C++ -> Code Generation -> Runtime Library).
- Если какие-то заголовки не найдены при сборке в VS, установите соответствующие компоненты (Windows SDK).

Если нужно, могу подготовить `.sln` или запустить удалённую сборку на Windows (через Actions или доступную машину).