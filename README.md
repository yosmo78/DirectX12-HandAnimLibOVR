Minimal Hand animation DirectX12 LibOVR oculus example from scratch using microsoft msvc++ compiler, just installing visual studio

Steps to compile and run:
1. Install visual studio 2022/2019 (if different, need to change the Compile.bat script)
2. Clone repo
3. Open command prompt in repo
4. Run: `.\Compile.bat`
5. While Oculus Headset is connected, Run: `.\BasicOVR.exe`

To Debug:
1. Run: `.\Compile.bat`
2. Run: devenv `.\BasicOVRDebug.exe`
3. While Oculus Headset is connected, When Visual Studio is running, press F11

Controls:
- Esc to pause/unpause
- Alt + F4 to quit, or just close it from task manager (or close from the oculus menu)

Improvements to make:
- All the same improvements as https://github.com/yosmo78/Win32DirectX12-FPSCamera and https://github.com/yosmo78/DirectX12-MinimalOculusLibOVR
