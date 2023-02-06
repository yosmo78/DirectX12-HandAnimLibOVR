@echo off
if not defined DevEnvDir (
	call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined DevEnvDir (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if "%Platform%" neq "x64" (
    echo ERROR: Platform is not "x64" - previous bat call failed.
    exit /b 1
)

set VERTEXSHADERSKINNED=VertexShaderSkinned.hlsl
set VERTEXSHADER=VertexShader.hlsl
set PIXELSHADER=PixelShader.hlsl
set FILES=main.cpp

set SHADERFLAGS=/WX /D__SHADER_TARGET_MAJOR=5 /D__SHADER_TARGET_MINOR=0 /DMAX_BONES=32
set RELEASEFLAGS=/O2 /DMAIN_DEBUG=0 /DMAX_BONES=32 /DAVX_ACTIVE=0 /DRUNTIME_DEBUG_COMPILE=0 /DCOMPILED_DEBUG_CSO=0
set AVXRELEASEFLAGS=/O2 /arch:AVX2 /DMAX_BONES=32 /DMAIN_DEBUG=0 /DAVX_ACTIVE=1 /DRUNTIME_DEBUG_COMPILE=0 /DCOMPILED_DEBUG_CSO=0
set DEBUGFLAGS=/Zi /DMAIN_DEBUG=1 /DMAX_BONES=32 /DAVX_ACTIVE=0 /DRUNTIME_DEBUG_COMPILE=0 /DCOMPILED_DEBUG_CSO=0

::TODO only link with d3dcompiler.lib if RUNTIME_DEBUG_COMPILE is 1
set LIBS=d3d12.lib dxgi.lib dxguid.lib kernel32.lib user32.lib gdi32.lib .\libOVR\LibOVR.lib

::TODO does dxc compiler produce better performing shader code?

::Release
fxc /nologo /T vs_5_0 /O3 %SHADERFLAGS% /Qstrip_reflect /Qstrip_debug /Qstrip_priv %VERTEXSHADER% /Fh vertShader.h /Vn vertexShaderBlob
fxc /nologo /T vs_5_0 /O3 %SHADERFLAGS% /Qstrip_reflect /Qstrip_debug /Qstrip_priv %VERTEXSHADERSKINNED% /Fh vertShaderSkinned.h /Vn vertexShaderSkinnedBlob
fxc /nologo /T ps_5_0 /O3 %SHADERFLAGS% /Qstrip_reflect /Qstrip_debug /Qstrip_priv %PIXELSHADER% /Fh pixelShader.h /Vn pixelShaderBlob
cl /nologo /W3 /GS- /Gs999999 %RELEASEFLAGS% %FILES% /Fe: BasicOVR.exe %LIBS% /I.\libOVR\Include /link /incremental:no /opt:icf /opt:ref /subsystem:windows

::Release AVX
fxc /nologo /T vs_5_0 /O3 %SHADERFLAGS% /Qstrip_reflect /Qstrip_debug /Qstrip_priv %VERTEXSHADER% /Fh vertShader.h /Vn vertexShaderBlob
fxc /nologo /T vs_5_0 /O3 %SHADERFLAGS% /Qstrip_reflect /Qstrip_debug /Qstrip_priv %VERTEXSHADERSKINNED% /Fh vertShaderSkinned.h /Vn vertexShaderSkinnedBlob
fxc /nologo /T ps_5_0 /O3 %SHADERFLAGS% /Qstrip_reflect /Qstrip_debug /Qstrip_priv %PIXELSHADER% /Fh pixelShader.h /Vn pixelShaderBlob
cl /nologo /W3 /GS- /Gs999999 %AVXRELEASEFLAGS% %FILES% /Fe: BasicOVRAVX2.exe %LIBS% /I.\libOVR\Include /link /incremental:no /opt:icf /opt:ref /subsystem:windows

::Debug
fxc /nologo /T vs_5_0 /Zi %SHADERFLAGS% %VERTEXSHADER% /Fh vertShaderDebug.h /Vn vertexShaderBlob
fxc /nologo /T vs_5_0 /Zi %SHADERFLAGS% %VERTEXSHADERSKINNED% /Fh vertShaderSkinnedDebug.h /Vn vertexShaderSkinnedBlob
fxc /nologo /T ps_5_0 /Zi %SHADERFLAGS% %PIXELSHADER% /Fh pixelShaderDebug.h /Vn pixelShaderBlob
cl /nologo /W3 /GS- /Gs999999 %DEBUGFLAGS% %FILES% /FC /Fe: BasicOVRDebug.exe %LIBS% /I.\libOVR\Include /link /incremental:no /opt:icf /opt:ref /subsystem:console
