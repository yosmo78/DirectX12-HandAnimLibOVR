//https://developer.oculus.com/documentation/native/pc/dg-libovr/
//https://developer.oculus.com/reference/libovr/1.43/o_v_r_c_a_p_i_8h/
//https://developer.oculus.com/documentation/native/pc/gsg-intro-oneworld/#gsg-intro-oneworld
//https://github.com/jherico/OculusSDK/blob/master/LibOVR/Include/OVR_ErrorCode.h
//https://developer.oculus.com/documentation/native/pc/dg-vr-focus/
//https://github.com/opentrack/LibOVR/blob/branch-140/LibOVR/Include/Extras/OVR_StereoProjection.h
//https://developer.oculus.com/blog/asynchronous-spacewarp/
//https://developer.oculus.com/blog/increasing-fidelity-with-constellation-tracked-controllers/

//TODO what is difference between OculusDLL and libovr?
//TODO when the headset gets unplugged do we need to reupload meshes?
//TODO do we want mixed reality for better tracking? look into this, may even be cool for the design overlaying


/*
The root arguments can be:

Descriptor tables: As described above, they hold an offset plus the number of descriptors into the descriptor heap.
Root descriptors: Only a small amount of descriptors can be stored directly in a root parameter. This saves the application the effort to put those descriptors into a descriptor heap and removes an indirection.
Root constants: Those are constants provided directly to the shaders without having to go through root descriptors or descriptor tables.

To achieve optimal performance, applications should generally sort the layout of the root parameters in decreasing order of change frequency.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

//windows
#include <windows.h>

#if !_M_X64
#error 64-BIT platform required!
#endif

//direct x
#include <d3d12.h>
#include <dxgi1_4.h>  //how low can we drop this...

#if MAIN_DEBUG
#include "vertShaderDebug.h" //in debug use .cso files for hot shader reloading for faster developing
#include "vertShaderSkinnedDebug.h"
#include "pixelShaderDebug.h"
#else
#include "vertShader.h"
#include "vertShaderSkinned.h"
#include "pixelShader.h"
#endif

#if !MAIN_DEBUG
#define NDEBUG
#endif

#include <stdint.h>
#include <math.h>
#if MAIN_DEBUG
#include <stdio.h>
#include <assert.h>
#endif
#include <stdlib.h> //if switching to mainCRT would have to replace with our own allocator, which is fine just use virtual alloc

// for struct references look in OVR_CAPI.h and 
#include "OVR_CAPI_D3D.h"

#define PI_F 3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679f
#define PI_D 3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32; //floating 32
typedef double   f64; //floating 64

typedef struct Mat3f
{
	union
	{
		f32 m[3][3];
	};
} Mat3f;

typedef struct Mat4f
{
	union
	{
		f32 m[4][4];
	};
} Mat4f;

typedef struct Mat3x4f
{
	union
	{
		f32 m[3][4];
	};
} Mat3x4f;

typedef struct Vec2f
{
	union
	{
		f32 v[2];
		struct
		{
			f32 x;
			f32 y;
		};
	};
} Vec2f;

typedef struct Vec3f
{
	union
	{
		f32 v[3];
		struct
		{
			f32 x;
			f32 y;
			f32 z;
		};
	};
} Vec3f;

typedef struct Vec4f
{
	union
	{
		f32 v[4];
		struct
		{
			f32 x;
			f32 y;
			f32 z;
			f32 w;
		};
	};
} Vec4f;

typedef struct Quatf
{
	union
	{
		f32 q[4];
		struct
		{
			f32 w; //real
			f32 x;
			f32 y;
			f32 z;
		};
		struct
		{
			f32 r; //real;
			Vec3f v;
		};
	};
} Quatf;

typedef struct Bone
{
	Quatf qLocalRot;
	Vec3f vLocalTrans;
	Vec3f vScale;
} Bone;

typedef struct KeyFrame
{
	Quatf qRot;
	Vec3f vPos;
} KeyFrame;

typedef struct vertexShaderCB
{
	Mat4f mvpMat;
	Mat3x4f nMat; //there is 3 floats of padding for 16 byte alignment;
} vertexShaderCB;

typedef struct pixelShaderCB
{
	Vec4f vLightColor;
	Vec3f vInvLightDir; //there is 3 floats of padding for 16 byte alignment;
} pixelShaderCB;


#include "Models.h"

//Game state
u8 Running;
u8 isPaused;

//Camera
Vec3f startingPos;
f32 rotHor;
f32 rotVert;
f32 fPlayerEyeHeight;


//Oculus Globals
u64 oculusFrameCount;
u32 oculusCurrentFrameIdx;
u32 oculusNUM_FRAMES;
ovrSession oculusSession; //oculus's global state variable
ovrGraphicsLuid oculusGLuid; //uid of graphics card that has headset attachted to it
ovrHmdDesc oculusHMDDesc; //description of the headset
ovrRecti oculusEyeRenderViewport[ovrEye_Count]; //TODO do I even need to store this!
D3D12_VIEWPORT EyeViewports[ovrEye_Count];
D3D12_RECT EyeScissorRects[ovrEye_Count];
ovrTextureSwapChain oculusEyeSwapChains[ovrEye_Count];
ID3D12Resource** oculusEyeBackBuffers;
ID3D12Resource* depthStencilBuffers[ovrEye_Count];

D3D12_CPU_DESCRIPTOR_HANDLE eyeStartingRTVHandle[ovrEye_Count];
D3D12_CPU_DESCRIPTOR_HANDLE eyeDSVHandle[ovrEye_Count];

//DirectX12 Globals
const u8 numSwapChains = 2; // we should allow users the ability to display the game on their screen, so change to 3 (1 for left eye, 1 for right eye, 1 for toggleable render window(when not displaying on screen a very small check box window is appearing saying toggle to render to screen too, then in options in game you can untoggle and turn it off))
ID3D12Device* device;
ID3D12CommandQueue* commandQueue;
ID3D12CommandAllocator** commandAllocators;
ID3D12GraphicsCommandList* commandLists[ovrEye_Count+1]; //one extra for model uploading, would be used for streaming!

//Heaps
ID3D12Heap* pSrvBoneHeap; //can structured buffer be done in root constants?
ID3D12Heap* pModelDefaultHeap;
ID3D12Heap* pModelUploadHeap; //will be <= size of the default heap

ID3D12Resource* defaultBuffer; //a default committed resource
ID3D12Resource* uploadBuffer; //a tmp upload committed resource
ID3D12Resource* boneBuffer[6][ovrHand_Count];

//views
D3D12_VERTEX_BUFFER_VIEW planeVertexBufferView;
D3D12_INDEX_BUFFER_VIEW planeIndexBufferView;
D3D12_VERTEX_BUFFER_VIEW cubeVertexBufferView;
D3D12_INDEX_BUFFER_VIEW cubeIndexBufferView;
D3D12_VERTEX_BUFFER_VIEW handVertexBufferView;
D3D12_INDEX_BUFFER_VIEW handIndexBufferView;

// D3D12 Descriptors
ID3D12DescriptorHeap* rtvDescriptorHeap;
u64 rtvDescriptorSize;
ID3D12DescriptorHeap* dsDescriptorHeap;
ID3D12DescriptorHeap* srvDescriptorHeap;
u64 srvDescriptorSize;

//pipeline info
const u32 dwSampleRate = 1;
ID3D12RootSignature* rootSignature; // root signature defines data shaders will access
ID3D12RootSignature* skinnedRootSignature;
ID3D12PipelineState* pipelineStateObject; // pso containing a pipeline state (a per material thing)
ID3D12PipelineState* skinnedPipelineStateObject; // pso containing a pipeline state

//Model Upload Syncronization
ID3D12Fence* streamingFence;
u64 currStreamingFenceValue;
u64 streamingFenceValue;
HANDLE fenceEvent;

//D3D12 Debug
#if MAIN_DEBUG
ID3D12Debug *debugInterface;
ID3D12InfoQueue *pIQueue; 
#endif

//Constant Buffers
vertexShaderCB vertexConstantBuffer;
pixelShaderCB pixelConstantBuffer;

Mat4f mHandFrameFinalBones[6][ovrHand_Count][handBonesCount]; //initialize these to first frame of animation!  //only use up to oculusNUM_FRAMES amount
f32 fPrevSideFingerDownAmount[6][ovrHand_Count] = { 0.0f }; //only use up to oculusNUM_FRAMES amount
f32 fPrevIndexFingerDownAmount[6][ovrHand_Count] = { 0.0f }; //only use up to oculusNUM_FRAMES amount

inline
void InitMat3f( Mat3f *a_pMat )
{
	a_pMat->m[0][0] = 1; a_pMat->m[0][1] = 0; a_pMat->m[0][2] = 0;
	a_pMat->m[1][0] = 0; a_pMat->m[1][1] = 1; a_pMat->m[1][2] = 0;
	a_pMat->m[2][0] = 0; a_pMat->m[2][1] = 0; a_pMat->m[2][2] = 1;
}

inline
void InitMat4f( Mat4f *a_pMat )
{
	a_pMat->m[0][0] = 1; a_pMat->m[0][1] = 0; a_pMat->m[0][2] = 0; a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0; a_pMat->m[1][1] = 1; a_pMat->m[1][2] = 0; a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0; a_pMat->m[2][1] = 0; a_pMat->m[2][2] = 1; a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = 0; a_pMat->m[3][1] = 0; a_pMat->m[3][2] = 0; a_pMat->m[3][3] = 1;
}

inline
void InitTransMat4f( Mat4f *a_pMat, f32 x, f32 y, f32 z )
{
	a_pMat->m[0][0] = 1; a_pMat->m[0][1] = 0; a_pMat->m[0][2] = 0; a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0; a_pMat->m[1][1] = 1; a_pMat->m[1][2] = 0; a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0; a_pMat->m[2][1] = 0; a_pMat->m[2][2] = 1; a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = x; a_pMat->m[3][1] = y; a_pMat->m[3][2] = z; a_pMat->m[3][3] = 1;
}

inline
void InitTransMat4f( Mat4f *a_pMat, Vec3f *a_pTrans )
{
	a_pMat->m[0][0] = 1;           a_pMat->m[0][1] = 0;           a_pMat->m[0][2] = 0;           a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0;           a_pMat->m[1][1] = 1;           a_pMat->m[1][2] = 0;           a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0;           a_pMat->m[2][1] = 0;           a_pMat->m[2][2] = 1;           a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = a_pTrans->x; a_pMat->m[3][1] = a_pTrans->y; a_pMat->m[3][2] = a_pTrans->z; a_pMat->m[3][3] = 1;
}

/*
inline
void InitRotXMat4f( Mat4f *a_pMat, f32 angle )
{
	a_pMat->m[0][0] = 1; a_pMat->m[0][1] = 0;                        a_pMat->m[0][2] = 0;                       a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0; a_pMat->m[1][1] = cosf(angle*PI_F/180.0f);  a_pMat->m[1][2] = sinf(angle*PI_F/180.0f); a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0; a_pMat->m[2][1] = -sinf(angle*PI_F/180.0f); a_pMat->m[2][2] = cosf(angle*PI_F/180.0f); a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = 0; a_pMat->m[3][1] = 0;                        a_pMat->m[3][2] = 0;                       a_pMat->m[3][3] = 1;
}

inline
void InitRotYMat4f( Mat4f *a_pMat, f32 angle )
{
	a_pMat->m[0][0] = cosf(angle*PI_F/180.0f);  a_pMat->m[0][1] = 0; a_pMat->m[0][2] = -sinf(angle*PI_F/180.0f); a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0;                        a_pMat->m[1][1] = 1; a_pMat->m[1][2] = 0;                        a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = sinf(angle*PI_F/180.0f);  a_pMat->m[2][1] = 0; a_pMat->m[2][2] = cosf(angle*PI_F/180.0f);  a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = 0;                        a_pMat->m[3][1] = 0; a_pMat->m[3][2] = 0;                        a_pMat->m[3][3] = 1;
}

inline
void InitRotZMat4f( Mat4f *a_pMat, f32 angle )
{
	a_pMat->m[0][0] = cosf(angle*PI_F/180.0f);  a_pMat->m[0][1] = sinf(angle*PI_F/180.0f); a_pMat->m[0][2] = 0; a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = -sinf(angle*PI_F/180.0f); a_pMat->m[1][1] = cosf(angle*PI_F/180.0f); a_pMat->m[1][2] = 0; a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0;                        a_pMat->m[2][1] = 0; 					   a_pMat->m[2][2] = 1; a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = 0;                        a_pMat->m[3][1] = 0;                       a_pMat->m[3][2] = 0; a_pMat->m[3][3] = 1;
}
*/

inline
void InitRotArbAxisMat4f( Mat4f *a_pMat, Vec3f *a_pAxis, f32 angle )
{
	f32 c = cosf(angle*PI_F/180.0f);
	f32 mC = 1.0f-c;
	f32 s = sinf(angle*PI_F/180.0f);
	a_pMat->m[0][0] = c                          + (a_pAxis->x*a_pAxis->x*mC); a_pMat->m[0][1] = (a_pAxis->y*a_pAxis->x*mC) + (a_pAxis->z*s);             a_pMat->m[0][2] = (a_pAxis->z*a_pAxis->x*mC) - (a_pAxis->y*s);             a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = (a_pAxis->x*a_pAxis->y*mC) - (a_pAxis->z*s);             a_pMat->m[1][1] = c                          + (a_pAxis->y*a_pAxis->y*mC); a_pMat->m[1][2] = (a_pAxis->z*a_pAxis->y*mC) + (a_pAxis->x*s);             a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = (a_pAxis->x*a_pAxis->z*mC) + (a_pAxis->y*s);             a_pMat->m[2][1] = (a_pAxis->y*a_pAxis->z*mC) - (a_pAxis->x*s);             a_pMat->m[2][2] = c                          + (a_pAxis->z*a_pAxis->z*mC); a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = 0;                                                       a_pMat->m[3][1] = 0;                                                       a_pMat->m[3][2] = 0;                                                       a_pMat->m[3][3] = 1;
}


//Following are DirectX Matrices
inline
void InitPerspectiveProjectionMat4fDirectXRH( Mat4f *a_pMat, u64 width, u64 height, f32 a_hFOV, f32 a_vFOV, f32 nearPlane, f32 farPlane )
{
	f32 thFOV = tanf(a_hFOV*PI_F/360);
	f32 tvFOV = tanf(a_vFOV*PI_F/360);
	f32 nMinF = farPlane/(nearPlane-farPlane);
  	f32 aspect = height / (f32)width;
	a_pMat->m[0][0] = aspect/(thFOV); a_pMat->m[0][1] = 0;            a_pMat->m[0][2] = 0;               a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0;              a_pMat->m[1][1] = 1.0f/(tvFOV); a_pMat->m[1][2] = 0;               a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0;              a_pMat->m[2][1] = 0;            a_pMat->m[2][2] = nMinF;           a_pMat->m[2][3] = -1.0f;
	a_pMat->m[3][0] = 0;              a_pMat->m[3][1] = 0;            a_pMat->m[3][2] = nearPlane*nMinF; a_pMat->m[3][3] = 0;
}

inline
void InitPerspectiveProjectionMat4fDirectXLH( Mat4f *a_pMat, u64 width, u64 height, f32 a_hFOV, f32 a_vFOV, f32 nearPlane, f32 farPlane )
{
	f32 thFOV = tanf(a_hFOV*PI_F/360);
	f32 tvFOV = tanf(a_vFOV*PI_F/360);
	f32 nMinF = farPlane/(nearPlane-farPlane);
  	f32 aspect = height / (f32)width;
	a_pMat->m[0][0] = aspect/(thFOV); a_pMat->m[0][1] = 0;            a_pMat->m[0][2] = 0;               a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0;              a_pMat->m[1][1] = 1.0f/(tvFOV); a_pMat->m[1][2] = 0;               a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 0;              a_pMat->m[2][1] = 0;            a_pMat->m[2][2] = -nMinF;          a_pMat->m[2][3] = 1.0f;
	a_pMat->m[3][0] = 0;              a_pMat->m[3][1] = 0;            a_pMat->m[3][2] = nearPlane*nMinF; a_pMat->m[3][3] = 0;
}

inline
void InitPerspectiveProjectionMat4fOculusDirectXLH( Mat4f *a_pMat, ovrFovPort tanHalfFov, f32 nearPlane, f32 farPlane )
{
    f32 projXScale = 2.0f / ( tanHalfFov.LeftTan + tanHalfFov.RightTan );
    f32 projXOffset = ( tanHalfFov.LeftTan - tanHalfFov.RightTan ) * projXScale * 0.5f;
    f32 projYScale = 2.0f / ( tanHalfFov.UpTan + tanHalfFov.DownTan );
    f32 projYOffset = ( tanHalfFov.UpTan - tanHalfFov.DownTan ) * projYScale * 0.5f;
	f32 nMinF = farPlane/(nearPlane-farPlane);
	a_pMat->m[0][0] = projXScale;  a_pMat->m[0][1] = 0;            a_pMat->m[0][2] = 0;               a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0;           a_pMat->m[1][1] = projYScale;   a_pMat->m[1][2] = 0;               a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = projXOffset; a_pMat->m[2][1] = -projYOffset; a_pMat->m[2][2] = -nMinF;          a_pMat->m[2][3] = 1.0f;
	a_pMat->m[3][0] = 0;           a_pMat->m[3][1] = 0;            a_pMat->m[3][2] = nearPlane*nMinF; a_pMat->m[3][3] = 0;
}


inline
void InitPerspectiveProjectionMat4fOculusDirectXRH( Mat4f *a_pMat, ovrFovPort tanHalfFov, f32 nearPlane, f32 farPlane )
{
    f32 projXScale = 2.0f / ( tanHalfFov.LeftTan + tanHalfFov.RightTan );
    f32 projXOffset = ( tanHalfFov.LeftTan - tanHalfFov.RightTan ) * projXScale * 0.5f;
    f32 projYScale = 2.0f / ( tanHalfFov.UpTan + tanHalfFov.DownTan );
    f32 projYOffset = ( tanHalfFov.UpTan - tanHalfFov.DownTan ) * projYScale * 0.5f;
	f32 nMinF = farPlane/(nearPlane-farPlane);
	a_pMat->m[0][0] = projXScale;   a_pMat->m[0][1] = 0;           a_pMat->m[0][2] = 0;               a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 0;            a_pMat->m[1][1] = projYScale;  a_pMat->m[1][2] = 0;               a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = -projXOffset; a_pMat->m[2][1] = projYOffset; a_pMat->m[2][2] = nMinF;           a_pMat->m[2][3] = -1.0f;
	a_pMat->m[3][0] = 0;            a_pMat->m[3][1] = 0;           a_pMat->m[3][2] = nearPlane*nMinF; a_pMat->m[3][3] = 0;
}

inline
f32 DeterminantUpper3x3Mat4f( Mat4f *a_pMat )
{
	return (a_pMat->m[0][0] * ((a_pMat->m[1][1]*a_pMat->m[2][2]) - (a_pMat->m[1][2]*a_pMat->m[2][1]))) + 
		   (a_pMat->m[0][1] * ((a_pMat->m[2][0]*a_pMat->m[1][2]) - (a_pMat->m[1][0]*a_pMat->m[2][2]))) + 
		   (a_pMat->m[0][2] * ((a_pMat->m[1][0]*a_pMat->m[2][1]) - (a_pMat->m[2][0]*a_pMat->m[1][1])));
}

inline
void InverseUpper3x3Mat4f( Mat4f *__restrict a_pMat, Mat4f *__restrict out )
{
	f32 fDet = DeterminantUpper3x3Mat4f( a_pMat );
#if MAIN_DEBUG
	assert( fDet != 0.f );
#endif
	f32 fInvDet = 1.0f / fDet;
	out->m[0][0] = fInvDet * ((a_pMat->m[1][1]*a_pMat->m[2][2]) - (a_pMat->m[1][2]*a_pMat->m[2][1]));
	out->m[0][1] = fInvDet * ((a_pMat->m[0][2]*a_pMat->m[2][1]) - (a_pMat->m[0][1]*a_pMat->m[2][2]));
	out->m[0][2] = fInvDet * ((a_pMat->m[0][1]*a_pMat->m[1][2]) - (a_pMat->m[0][2]*a_pMat->m[1][1]));
	out->m[0][3] = 0.0f;

	out->m[1][0] = fInvDet * ((a_pMat->m[2][0]*a_pMat->m[1][2]) - (a_pMat->m[2][2]*a_pMat->m[1][0]));
	out->m[1][1] = fInvDet * ((a_pMat->m[0][0]*a_pMat->m[2][2]) - (a_pMat->m[0][2]*a_pMat->m[2][0])); 
	out->m[1][2] = fInvDet * ((a_pMat->m[0][2]*a_pMat->m[1][0]) - (a_pMat->m[1][2]*a_pMat->m[0][0]));
	out->m[1][3] = 0.0f;

	out->m[2][0] = fInvDet * ((a_pMat->m[1][0]*a_pMat->m[2][1]) - (a_pMat->m[1][1]*a_pMat->m[2][0]));
	out->m[2][1] = fInvDet * ((a_pMat->m[0][1]*a_pMat->m[2][0]) - (a_pMat->m[0][0]*a_pMat->m[2][1]));
	out->m[2][2] = fInvDet * ((a_pMat->m[0][0]*a_pMat->m[1][1]) - (a_pMat->m[1][0]*a_pMat->m[0][1]));
	out->m[2][3] = 0.0f;

	out->m[3][0] = 0.0f;
	out->m[3][1] = 0.0f;
	out->m[3][2] = 0.0f;
	out->m[3][3] = 1.0f;
}

inline
void InverseTransposeUpper3x3Mat4f( Mat4f *__restrict a_pMat, Mat4f *__restrict out )
{
	f32 fDet = DeterminantUpper3x3Mat4f( a_pMat );
#if MAIN_DEBUG
	assert( fDet != 0.f );
#endif
	f32 fInvDet = 1.0f / fDet;
	out->m[0][0] = fInvDet * ((a_pMat->m[1][1]*a_pMat->m[2][2]) - (a_pMat->m[1][2]*a_pMat->m[2][1]));
	out->m[0][1] = fInvDet * ((a_pMat->m[2][0]*a_pMat->m[1][2]) - (a_pMat->m[2][2]*a_pMat->m[1][0]));
	out->m[0][2] = fInvDet * ((a_pMat->m[1][0]*a_pMat->m[2][1]) - (a_pMat->m[1][1]*a_pMat->m[2][0]));
	out->m[0][3] = 0.0f;

	out->m[1][0] = fInvDet * ((a_pMat->m[0][2]*a_pMat->m[2][1]) - (a_pMat->m[0][1]*a_pMat->m[2][2]));
	out->m[1][1] = fInvDet * ((a_pMat->m[0][0]*a_pMat->m[2][2]) - (a_pMat->m[0][2]*a_pMat->m[2][0])); 
	out->m[1][2] = fInvDet * ((a_pMat->m[0][1]*a_pMat->m[2][0]) - (a_pMat->m[0][0]*a_pMat->m[2][1]));
	out->m[1][3] = 0.0f;

	out->m[2][0] = fInvDet * ((a_pMat->m[0][1]*a_pMat->m[1][2]) - (a_pMat->m[0][2]*a_pMat->m[1][1]));
	out->m[2][1] = fInvDet * ((a_pMat->m[0][2]*a_pMat->m[1][0]) - (a_pMat->m[1][2]*a_pMat->m[0][0]));
	out->m[2][2] = fInvDet * ((a_pMat->m[0][0]*a_pMat->m[1][1]) - (a_pMat->m[1][0]*a_pMat->m[0][1]));
	out->m[2][3] = 0.0f;

	out->m[3][0] = 0.0f;
	out->m[3][1] = 0.0f;
	out->m[3][2] = 0.0f;
	out->m[3][3] = 1.0f;
}

inline
void InverseTransposeUpper3x3Mat4f( Mat4f *__restrict a_pMat, Mat3x4f *__restrict out )
{
	f32 fDet = DeterminantUpper3x3Mat4f( a_pMat );
#if MAIN_DEBUG
	assert( fDet != 0.f );
#endif
	f32 fInvDet = 1.0f / fDet;
	out->m[0][0] = fInvDet * ((a_pMat->m[1][1]*a_pMat->m[2][2]) - (a_pMat->m[1][2]*a_pMat->m[2][1]));
	out->m[0][1] = fInvDet * ((a_pMat->m[2][0]*a_pMat->m[1][2]) - (a_pMat->m[2][2]*a_pMat->m[1][0]));
	out->m[0][2] = fInvDet * ((a_pMat->m[1][0]*a_pMat->m[2][1]) - (a_pMat->m[1][1]*a_pMat->m[2][0]));
	out->m[0][3] = 0.0f;

	out->m[1][0] = fInvDet * ((a_pMat->m[0][2]*a_pMat->m[2][1]) - (a_pMat->m[0][1]*a_pMat->m[2][2]));
	out->m[1][1] = fInvDet * ((a_pMat->m[0][0]*a_pMat->m[2][2]) - (a_pMat->m[0][2]*a_pMat->m[2][0])); 
	out->m[1][2] = fInvDet * ((a_pMat->m[0][1]*a_pMat->m[2][0]) - (a_pMat->m[0][0]*a_pMat->m[2][1]));
	out->m[1][3] = 0.0f;

	out->m[2][0] = fInvDet * ((a_pMat->m[0][1]*a_pMat->m[1][2]) - (a_pMat->m[0][2]*a_pMat->m[1][1]));
	out->m[2][1] = fInvDet * ((a_pMat->m[0][2]*a_pMat->m[1][0]) - (a_pMat->m[1][2]*a_pMat->m[0][0]));
	out->m[2][2] = fInvDet * ((a_pMat->m[0][0]*a_pMat->m[1][1]) - (a_pMat->m[1][0]*a_pMat->m[0][1]));
	out->m[2][3] = 0.0f;
}


inline
void Mat4fMult( Mat4f *__restrict a, Mat4f *__restrict b, Mat4f *__restrict out)
{
	out->m[0][0] = a->m[0][0]*b->m[0][0] + a->m[0][1]*b->m[1][0] + a->m[0][2]*b->m[2][0] + a->m[0][3]*b->m[3][0];
	out->m[0][1] = a->m[0][0]*b->m[0][1] + a->m[0][1]*b->m[1][1] + a->m[0][2]*b->m[2][1] + a->m[0][3]*b->m[3][1];
	out->m[0][2] = a->m[0][0]*b->m[0][2] + a->m[0][1]*b->m[1][2] + a->m[0][2]*b->m[2][2] + a->m[0][3]*b->m[3][2];
	out->m[0][3] = a->m[0][0]*b->m[0][3] + a->m[0][1]*b->m[1][3] + a->m[0][2]*b->m[2][3] + a->m[0][3]*b->m[3][3];

	out->m[1][0] = a->m[1][0]*b->m[0][0] + a->m[1][1]*b->m[1][0] + a->m[1][2]*b->m[2][0] + a->m[1][3]*b->m[3][0];
	out->m[1][1] = a->m[1][0]*b->m[0][1] + a->m[1][1]*b->m[1][1] + a->m[1][2]*b->m[2][1] + a->m[1][3]*b->m[3][1];
	out->m[1][2] = a->m[1][0]*b->m[0][2] + a->m[1][1]*b->m[1][2] + a->m[1][2]*b->m[2][2] + a->m[1][3]*b->m[3][2];
	out->m[1][3] = a->m[1][0]*b->m[0][3] + a->m[1][1]*b->m[1][3] + a->m[1][2]*b->m[2][3] + a->m[1][3]*b->m[3][3];

	out->m[2][0] = a->m[2][0]*b->m[0][0] + a->m[2][1]*b->m[1][0] + a->m[2][2]*b->m[2][0] + a->m[2][3]*b->m[3][0];
	out->m[2][1] = a->m[2][0]*b->m[0][1] + a->m[2][1]*b->m[1][1] + a->m[2][2]*b->m[2][1] + a->m[2][3]*b->m[3][1];
	out->m[2][2] = a->m[2][0]*b->m[0][2] + a->m[2][1]*b->m[1][2] + a->m[2][2]*b->m[2][2] + a->m[2][3]*b->m[3][2];
	out->m[2][3] = a->m[2][0]*b->m[0][3] + a->m[2][1]*b->m[1][3] + a->m[2][2]*b->m[2][3] + a->m[2][3]*b->m[3][3];

	out->m[3][0] = a->m[3][0]*b->m[0][0] + a->m[3][1]*b->m[1][0] + a->m[3][2]*b->m[2][0] + a->m[3][3]*b->m[3][0];
	out->m[3][1] = a->m[3][0]*b->m[0][1] + a->m[3][1]*b->m[1][1] + a->m[3][2]*b->m[2][1] + a->m[3][3]*b->m[3][1];
	out->m[3][2] = a->m[3][0]*b->m[0][2] + a->m[3][1]*b->m[1][2] + a->m[3][2]*b->m[2][2] + a->m[3][3]*b->m[3][2];
	out->m[3][3] = a->m[3][0]*b->m[0][3] + a->m[3][1]*b->m[1][3] + a->m[3][2]*b->m[2][3] + a->m[3][3]*b->m[3][3];
}

inline
void Vec3fAdd( Vec3f *a, Vec3f *b, Vec3f *out )
{
	out->x = a->x + b->x;
	out->y = a->y + b->y;
	out->z = a->z + b->z;
}

inline
void Vec3fSub( Vec3f *a, Vec3f *b, Vec3f *out )
{
	out->x = a->x - b->x;
	out->y = a->y - b->y;
	out->z = a->z - b->z;
}

inline
void Vec3fMult( Vec3f *a, Vec3f *b, Vec3f *out )
{
	out->x = a->x * b->x;
	out->y = a->y * b->y;
	out->z = a->z * b->z;
}

inline
void Vec3fCross( Vec3f *a, Vec3f *b, Vec3f *out )
{
	out->x = (a->y * b->z) - (a->z * b->y);
	out->y = (a->z * b->x) - (a->x * b->z);
	out->z = (a->x * b->y) - (a->y * b->x);
}

inline
void Vec3fScale( Vec3f *a, f32 scale, Vec3f *out )
{
	out->x = a->x * scale;
	out->y = a->y * scale;
	out->z = a->z * scale;
}


inline
void Vec3fScaleAdd( Vec3f *a, f32 scale, Vec3f *b, Vec3f *out )
{
	out->x = (a->x * scale) + b->x;
	out->y = (a->y * scale) + b->y;
	out->z = (a->z * scale) + b->z;
}

inline
f32 Vec3fDot( Vec3f *a, Vec3f *b )
{
	return (a->x * b->x) + (a->y * b->y) + (a->z * b->z);
}

inline
void Vec3fNormalize( Vec3f *a, Vec3f *out )
{

	f32 mag = sqrtf((a->x*a->x) + (a->y*a->y) + (a->z*a->z));
	if(mag == 0)
	{
		out->x = 0;
		out->y = 0;
		out->z = 0;
	}
	else
	{
		out->x = a->x/mag;
		out->y = a->y/mag;
		out->z = a->z/mag;
	}
}


inline
void Vec3fLerp( Vec3f *a, Vec3f *b, f32 fT, Vec3f *out )
{
	Vec3f vTmp;
	Vec3fSub(b,a,&vTmp);
	Vec3fScaleAdd(&vTmp,fT,a,out);
}

inline
void Vec3fRotByUnitQuat(Vec3f *v, Quatf *__restrict q, Vec3f *out)
{
    f32 fVecScalar = (2.0f*q->w*q->w)-1;
    f32 fQuatVecScalar = 2.0f* Vec3fDot(v,&q->v);

    Vec3f vScaledQuatVec;
    Vec3f vScaledVec;
    Vec3fScale(&q->v,fQuatVecScalar,&vScaledQuatVec);
    Vec3fScale(v,fVecScalar,&vScaledVec);

    Vec3f vQuatCrossVec;
    Vec3fCross(&q->v, v, &vQuatCrossVec);

    Vec3fScale(&vQuatCrossVec,2.0f*q->w,&vQuatCrossVec);

    Vec3fAdd(&vScaledQuatVec,&vScaledVec,out);
    Vec3fAdd(out,&vQuatCrossVec,out);
}

/*
inline
void Vec3fRotByUnitQuat(Vec3f *v, Quatf *__restrict q, Vec3f *out)
{
	Vec3f vDoubleRot;
	vDoubleRot.x = q->x + q->x;
	vDoubleRot.y = q->y + q->y;
	vDoubleRot.z = q->z + q->z;

	Vec3f vScaledWRot;
	vScaledWRot.x = q->w * vDoubleRot.x;
	vScaledWRot.y = q->w * vDoubleRot.y;
	vScaledWRot.z = q->w * vDoubleRot.z;

	Vec3f vScaledXRot;
	vScaledXRot.x = q->x * vDoubleRot.x;
	vScaledXRot.y = q->x * vDoubleRot.y;
	vScaledXRot.z = q->x * vDoubleRot.z;

	f32 fScaledYRot0 = q->y * vDoubleRot.y;
	f32 fScaledYRot1 = q->y * vDoubleRot.z;

	f32 fScaledZRot0 = q->z * vDoubleRot.z;

	out->x = ((v->x * ((1.f - fScaledYRot0) - fScaledZRot0)) + (v->y * (vScaledXRot.y - vScaledWRot.z))) + (v->z * (vScaledXRot.z + vScaledWRot.y));
	out->y = ((v->x * (vScaledXRot.y + vScaledWRot.z)) + (v->y * ((1.f - vScaledXRot.x) - fScaledZRot0))) + (v->z * (fScaledYRot1 - vScaledWRot.x));
	out->z = ((v->x * (vScaledXRot.z - vScaledWRot.y)) + (v->y * (fScaledYRot1 + vScaledWRot.x))) + (v->z * ((1.f - vScaledXRot.x) - fScaledYRot0));
}
*/


inline
void InitUnitQuatf( Quatf *q, f32 angle, Vec3f *axis )
{
	f32 s = sinf(angle*PI_F/360.0f);
	q->w = cosf(angle*PI_F/360.0f);
	q->x = axis->x * s;
	q->y = axis->y * s;
	q->z = axis->z * s;
}

inline
void QuatfMult( Quatf *__restrict a, Quatf *__restrict b, Quatf *__restrict out )
{
	out->w = (a->w * b->w) - (a->x* b->x) - (a->y* b->y) - (a->z* b->z);
	out->x = (a->w * b->x) + (a->x* b->w) + (a->y* b->z) - (a->z* b->y);
	out->y = (a->w * b->y) + (a->y* b->w) + (a->z* b->x) - (a->x* b->z);
	out->z = (a->w * b->z) + (a->z* b->w) + (a->x* b->y) - (a->y* b->x);
}

inline
void QuatfSub( Quatf *a, Quatf *b, Quatf *out )
{
	out->w = a->w - b->w;
	out->x = a->x - b->x;
	out->y = a->y - b->y;
	out->z = a->z - b->z;
}

inline
void QuatfScaleAdd( Quatf *a, f32 scale, Quatf *b, Quatf *out )
{
	out->w = (a->w * scale) + b->w;
	out->x = (a->x * scale) + b->x;
	out->y = (a->y * scale) + b->y;
	out->z = (a->z * scale) + b->z;
}

inline
void QuatfNormalize( Quatf *a, Quatf *out )
{

	f32 mag = sqrtf((a->w*a->w) + (a->x*a->x) + (a->y*a->y) + (a->z*a->z));
	if(mag == 0.f)
	{
		out->w = 0.f;
		out->x = 0.f;
		out->y = 0.f;
		out->z = 0.f;
	}
	else
	{
		out->w = a->w/mag;
		out->x = a->x/mag;
		out->y = a->y/mag;
		out->z = a->z/mag;
	}
}

//todo simplify to reduce floating point error
inline
void InitViewMat4ByQuatf( Mat4f *a_pMat, Quatf *a_qRot, Vec3f *a_pPos )
{
	a_pMat->m[0][0] = 1.0f - 2.0f*(a_qRot->y*a_qRot->y + a_qRot->z*a_qRot->z);                            a_pMat->m[0][1] = 2.0f*(a_qRot->x*a_qRot->y - a_qRot->w*a_qRot->z);                                   a_pMat->m[0][2] = 2.0f*(a_qRot->x*a_qRot->z + a_qRot->w*a_qRot->y);        		                      a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 2.0f*(a_qRot->x*a_qRot->y + a_qRot->w*a_qRot->z);                                   a_pMat->m[1][1] = 1.0f - 2.0f*(a_qRot->x*a_qRot->x + a_qRot->z*a_qRot->z);                            a_pMat->m[1][2] = 2.0f*(a_qRot->y*a_qRot->z - a_qRot->w*a_qRot->x);        		                      a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 2.0f*(a_qRot->x*a_qRot->z - a_qRot->w*a_qRot->y);                                   a_pMat->m[2][1] = 2.0f*(a_qRot->y*a_qRot->z + a_qRot->w*a_qRot->x);                                   a_pMat->m[2][2] = 1.0f - 2.0f*(a_qRot->x*a_qRot->x + a_qRot->y*a_qRot->y); 		                      a_pMat->m[2][3] = 0;
	a_pMat->m[3][0] = -a_pPos->x*a_pMat->m[0][0] - a_pPos->y*a_pMat->m[1][0] - a_pPos->z*a_pMat->m[2][0]; a_pMat->m[3][1] = -a_pPos->x*a_pMat->m[0][1] - a_pPos->y*a_pMat->m[1][1] - a_pPos->z*a_pMat->m[2][1]; a_pMat->m[3][2] = -a_pPos->x*a_pMat->m[0][2] - a_pPos->y*a_pMat->m[1][2] - a_pPos->z*a_pMat->m[2][2]; a_pMat->m[3][3] = 1;
}

inline
void InitModelMat4ByQuatf( Mat4f *a_pMat, Quatf *a_qRot, Vec3f *a_pPos )
{
	a_pMat->m[0][0] = 1.0f - 2.0f*(a_qRot->y*a_qRot->y + a_qRot->z*a_qRot->z);                            a_pMat->m[0][1] = 2.0f*(a_qRot->x*a_qRot->y + a_qRot->w*a_qRot->z);                                   a_pMat->m[0][2] = 2.0f*(a_qRot->x*a_qRot->z - a_qRot->w*a_qRot->y);        		                      a_pMat->m[0][3] = 0;
	a_pMat->m[1][0] = 2.0f*(a_qRot->x*a_qRot->y - a_qRot->w*a_qRot->z);                                   a_pMat->m[1][1] = 1.0f - 2.0f*(a_qRot->x*a_qRot->x + a_qRot->z*a_qRot->z);                            a_pMat->m[1][2] = 2.0f*(a_qRot->y*a_qRot->z + a_qRot->w*a_qRot->x);        		                      a_pMat->m[1][3] = 0;
	a_pMat->m[2][0] = 2.0f*(a_qRot->x*a_qRot->z + a_qRot->w*a_qRot->y);                                   a_pMat->m[2][1] = 2.0f*(a_qRot->y*a_qRot->z - a_qRot->w*a_qRot->x);                                   a_pMat->m[2][2] = 1.0f - 2.0f*(a_qRot->x*a_qRot->x + a_qRot->y*a_qRot->y); 		                      a_pMat->m[2][3] = 0;
	//a_pMat->m[3][0] = a_pPos->x*a_pMat->m[0][0] + a_pPos->y*a_pMat->m[1][0] + a_pPos->z*a_pMat->m[2][0];  a_pMat->m[3][1] = a_pPos->x*a_pMat->m[0][1] + a_pPos->y*a_pMat->m[1][1] + a_pPos->z*a_pMat->m[2][1]; a_pMat->m[3][2] = a_pPos->x*a_pMat->m[0][2] + a_pPos->y*a_pMat->m[1][2] + a_pPos->z*a_pMat->m[2][2]; a_pMat->m[3][3] = 1;
	a_pMat->m[3][0] = a_pPos->x;  a_pMat->m[3][1] = a_pPos->y; a_pMat->m[3][2] = a_pPos->z; a_pMat->m[3][3] = 1.f;
}

inline
void QuatfNormLerp( Quatf *a, Quatf *b, f32 fT, Quatf *out )
{
	Quatf qTmp;
	QuatfSub(b,a,&qTmp);
	QuatfScaleAdd(&qTmp,fT,a,out);
	QuatfNormalize(out,out);
}

inline
void QuatfSlerp( Vec3f *a, Vec3f *b, f32 fT, Vec3f *out )
{
	//todo
}

#if MAIN_DEBUG
void PrintMat4f( Mat4f *a_pMat )
{
	for( u32 dwIdx = 0; dwIdx < 4; ++dwIdx )
	{
		for( u32 dwJdx = 0; dwJdx < 4; ++dwJdx )
		{
			printf("%f ", a_pMat->m[dwIdx][dwJdx] );
		}
		printf("\n");
	}
}
#endif

f32 clamp(f32 d, f32 min, f32 max) {
  const f32 t = d < min ? min : d;
  return t > max ? max : t;
}

u32 max( u32 a, u32 b )
{
	return a > b ? a : b;
}


int logError(const char* msg)
{
#if MAIN_DEBUG
	MessageBoxA(NULL, msg, "Error", MB_OK | MB_ICONERROR);
#endif
    return -1;
}

void CloseProgram()
{
	Running = 0;
}

void Pause()
{
	if( !isPaused )
	{
		isPaused = 1;
	}
}

void UnPause()
{
	if( isPaused )
	{
		isPaused = 0;
	}
}

void TogglePause()
{
    isPaused = isPaused ^ 1;
}

inline
void InitStartingGameState()
{
	Running = 1;
    isPaused = 0;
}

inline
void InitHeadsetGraphicsState()
{
	pixelConstantBuffer.vLightColor = {0.83137f,0.62745f,0.09020f,1.0f};
	pixelConstantBuffer.vInvLightDir = {0.57735026919f,0.57735026919f,0.57735026919f};
}

inline
void InitStartingCamera()
{
	startingPos = { 0, 0, 0 };
	rotHor = 0;
	rotVert = 0;
	fPlayerEyeHeight = 1.643f; //avg US male height 1.753m - 11 cm (this will be overwritten but just as a default value)
}

inline 
void InitStartingSkeletons( u32 dwNumFrames )
{
	Mat4f mHandFrameBindBones[ovrHand_Count][handBonesCount];
	for( u32 dwHand = 0; dwHand < ovrHand_Count; ++dwHand )
	{
		InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans );
		Mat4fMult(&handInvBind[0], &mHandFrameBindBones[dwHand][0],&mHandFrameFinalBones[0][dwHand][0]);
		for( u32 dwBone = firstInnerBone; dwBone < (firstInnerBone+numInnerChannels); ++dwBone )
		{				
			Mat4f mLocalFrameBone;
			InitModelMat4ByQuatf( &mLocalFrameBone, &handInnerKeyFrames[0][dwBone-firstInnerBone].qRot, &handInnerKeyFrames[0][dwBone-firstInnerBone].vPos );
			Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
			Mat4fMult(&handInvBind[dwBone],&mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
		}
		for( u32 dwBone = firstOutterBone; dwBone < (firstOutterBone+numOutterChannels); ++dwBone )
		{				
			Mat4f mLocalFrameBone;
			InitModelMat4ByQuatf( &mLocalFrameBone, &handOutterKeyFrames[0][dwBone-firstOutterBone].qRot, &handOutterKeyFrames[0][dwBone-firstOutterBone].vPos );
			Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
			Mat4fMult(&handInvBind[dwBone],&mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
		}

		u8* pUploadBoneBufferData;
		if( FAILED( boneBuffer[0][dwHand]->Map( 0, nullptr, (void**) &pUploadBoneBufferData ) ) )
		{
		    return;
		}
		memcpy(pUploadBoneBufferData,&mHandFrameFinalBones[0][dwHand],sizeof(Mat4f)*handBonesCount);
		boneBuffer[0][dwHand]->Unmap( 0, nullptr );

		fPrevSideFingerDownAmount[0][dwHand] = 0.0f;
		fPrevIndexFingerDownAmount[0][dwHand] = 0.0f; 
	}
	for( u32 dwFrame = 1; dwFrame < dwNumFrames; ++dwFrame )
	{
		for( u32 dwHand = 0; dwHand < ovrHand_Count; ++dwHand )
		{
			for( u32 dwBone = 0; dwBone < handBonesCount; ++dwBone )
			{	
				mHandFrameFinalBones[dwFrame][dwHand][dwBone] = mHandFrameFinalBones[0][dwHand][dwBone];
			}

			u8* pUploadBoneBufferData;
			if( FAILED( boneBuffer[dwFrame][dwHand]->Map( 0, nullptr, (void**) &pUploadBoneBufferData ) ) )
			{
			    return;
			}
			memcpy(pUploadBoneBufferData,&mHandFrameFinalBones[dwFrame][dwHand],sizeof(Mat4f)*handBonesCount);
			boneBuffer[dwFrame][dwHand]->Unmap( 0, nullptr );

			fPrevSideFingerDownAmount[dwFrame][dwHand] = 0.0f;
			fPrevIndexFingerDownAmount[dwFrame][dwHand] = 0.0f; 
		}
	}
}

//verify the following are right!
inline
bool SignalStreamingFence()
{
	++streamingFenceValue;
    if( FAILED( commandQueue->Signal( streamingFence, streamingFenceValue ) ) )
    {
    	CloseProgram();
    	logError( "Error signalling fence!\n" ); 
    	return false;
    }
    return true;
}

inline
bool WaitStreamingFence()
{
	if( streamingFence->GetCompletedValue() < streamingFenceValue )
    {
    	if( FAILED( streamingFence->SetEventOnCompletion( streamingFenceValue, fenceEvent ) ) )
		{
			CloseProgram();
			logError( "Failed to set fence event!\n" );
			return false;
		}
		WaitForSingleObject( fenceEvent, INFINITE );
    }
    return true;
}

//append a signal to the end of the command queue and wait for it
inline
bool FlushStreamingCommandQueue()
{
   	if( !SignalStreamingFence() )
   	{
   		return false;
   	}
   	if( !WaitStreamingFence() )
   	{
		return false;
   	}
   	return true;
}


u8 InitOculusHeadset()
{
	oculusFrameCount = 0;
	if( ovr_Create( &oculusSession, &oculusGLuid ) < 0 )
	{
#if MAIN_DEBUG
    	//TODO change to a retry create head set
    	printf("ovr_Create failed\n");
#endif		
    	return 1;
	}

	//Get Head Mounted Display Description
	oculusHMDDesc = ovr_GetHmdDesc( oculusSession );
#if MAIN_DEBUG
	printf( "Headset: %s\n",&oculusHMDDesc.ProductName[0] );
	printf( "Made By: %s\n",&oculusHMDDesc.Manufacturer[0] );
	printf( "Firmware Ver: %d.%d\n",oculusHMDDesc.FirmwareMajor,oculusHMDDesc.FirmwareMinor );
	printf( "HMD Resolution: %d x %d\n",oculusHMDDesc.Resolution.w, oculusHMDDesc.Resolution.h );
	for( u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye )
	{
		printf("Eye %d stats:\n", dwEye);
		f32 fUpFOV    = (360.f / PI_F) * atanf( oculusHMDDesc.DefaultEyeFov[dwEye].UpTan );
		f32 fDownFOV  = (360.f / PI_F) * atanf( oculusHMDDesc.DefaultEyeFov[dwEye].DownTan );
		f32 fRightFOV = (360.f / PI_F) * atanf( oculusHMDDesc.DefaultEyeFov[dwEye].RightTan );
		f32 fLeftFOV  = (360.f / PI_F) * atanf( oculusHMDDesc.DefaultEyeFov[dwEye].LeftTan );
		f32 fMaxUpFOV    = (360.f / PI_F) * atanf( oculusHMDDesc.MaxEyeFov[dwEye].UpTan );
		f32 fMaxDownFOV  = (360.f / PI_F) * atanf( oculusHMDDesc.MaxEyeFov[dwEye].DownTan );
		f32 fMaxRightFOV = (360.f / PI_F) * atanf( oculusHMDDesc.MaxEyeFov[dwEye].RightTan );
		f32 fMaxLeftFOV  = (360.f / PI_F) * atanf( oculusHMDDesc.MaxEyeFov[dwEye].LeftTan );
		printf("    Default FOV: Up %f Down %f Left %f Right %f\n", fUpFOV, fDownFOV, fRightFOV, fLeftFOV );
		printf("    Max FOV: Up %f Down %f Left %f Right %f\n", fMaxUpFOV, fMaxDownFOV, fMaxRightFOV, fMaxLeftFOV );
	}
#endif
	return 0;
}

#if MAIN_DEBUG
inline
bool EnableDebugLayer()
{
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    if ( FAILED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugInterface ) ) ) )
    {
    	logError( "Error creating DirectX12 Debug layer\n" );  
        return false;
    }
    debugInterface->EnableDebugLayer();
    return true;
}


void PrintDirectXErrorCode(HRESULT errCode)
{
	switch(errCode)
	{
		case D3D12_ERROR_ADAPTER_NOT_FOUND:
		{
			printf("D3D12_ERROR_ADAPTER_NOT_FOUND - The specified cached PSO was created on a different adapter and cannot be reused on the current adapter\n");
			break;
		}
		case D3D12_ERROR_DRIVER_VERSION_MISMATCH:
		{
			printf("D3D12_ERROR_DRIVER_VERSION_MISMATCH - The specified cached PSO was created on a different driver version and cannot be reused on the current adapter.\n");
			break;
		}
		case DXGI_ERROR_DEVICE_REMOVED:
		{
			printf("DXGI_ERROR_DEVICE_REMOVED - Device Removed.\n");
			break;
		} 
		case DXGI_ERROR_INVALID_CALL: //DXGI_ERROR_INVALID_CALL
		{
			printf("D3DERR_INVALIDCALL (replaced with DXGI_ERROR_INVALID_CALL) - The method call is invalid. For example, a method's parameter may not be a valid pointer.\n");
			break;
		}
		case DXGI_ERROR_WAS_STILL_DRAWING:
		{
			printf("D3DERR_WASSTILLDRAWING (replaced with DXGI_ERROR_WAS_STILL_DRAWING) - The previous blit operation that is transferring information to or from this surface is incomplete.\n");
			break;
		}
		case E_FAIL:
		{
			printf("E_FAIL - Attempted to create a device with the debug layer enabled and the layer is not installed.\n");
			break;
		}
		case E_INVALIDARG:
		{
			printf("E_INVALIDARG - An invalid parameter was passed to the returning function.\n");
			break;
		}
		case E_OUTOFMEMORY:
		{
			printf("E_OUTOFMEMORY - Direct3D could not allocate sufficient memory to complete the call.\n");
			break;
		}
		case E_NOTIMPL:
		{
			printf("E_NOTIMPL - The method call isn't implemented with the passed parameter combination.\n");
			break;
		}
		case S_FALSE:
		{
			printf("S_FALSE	- Alternate success value, indicating a successful but nonstandard completion (the precise meaning depends on context).\n");
			break;
		}
		case S_OK:
		{
			break;
		}
		default:
		{
			printf("Unknown error %u\n",errCode);
			break;
		}
	}
}

#endif

inline
ID3D12CommandQueue *InitDirectCommandQueue( ID3D12Device* dxd3Device )
{
	ID3D12CommandQueue *cq;
	D3D12_COMMAND_QUEUE_DESC cqDesc;
    cqDesc.Type =     D3D12_COMMAND_LIST_TYPE_DIRECT;
    cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    cqDesc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;
    cqDesc.NodeMask = 0;

    if( FAILED( dxd3Device->CreateCommandQueue( &cqDesc, IID_PPV_ARGS( &cq ) ) ) )
	{
		return NULL;
	}
	return cq;
}

inline
ID3D12DescriptorHeap *InitRenderTargetDescriptorHeap( ID3D12Device* dxd3Device, u32 dwAmt )
{
	ID3D12DescriptorHeap *rtvHeap;
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = dwAmt; 
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	if( FAILED( dxd3Device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &rtvHeap ) ) ) )
	{
		return NULL;
	}
	return rtvHeap;
}


inline
ID3D12DescriptorHeap *InitDepthStencilDescriptorHeap( ID3D12Device* dxd3Device, u32 dwAmt )
{
	ID3D12DescriptorHeap *dsvHeap;
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NumDescriptors = dwAmt;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	if( FAILED( dxd3Device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &dsvHeap ) ) ) )
	{
		return NULL;
	}
	return dsvHeap;
}

inline
ID3D12DescriptorHeap *InitSRVDescriptorHeap()
{
	return nullptr;
}


inline
bool CreateDepthStencilBuffer()
{
	/*
	frame time->
	|--- prev ---|--- curr ---|--- next ---|

	we only need 1 depth buffer currently, since the cpu is not mutating it while it is actively rendering (unlike uniforms). 
	and it is reset within the command lists and only actively used within the current frame rendering. (it is synchronous within the command list, this is not always the case, but here it is) (unlike swap chain buffers which cannot be mutated until after the screen is done with them)

	only 1 frame will be using it as a time, since one frame doesn't start rendering until the last one is finished executing its command lists (maybe in future programs, this won't be true)
	
	hence we only need 1 depth buffer here.

	"Since we are only using a single command queue for rendering, then all of the rendering commands are executed serially in the queue so the depth buffer will not be accessed by multiple queues at the same time. The reason why we need multiple color buffers is because we can’t write the next frame until the Windows Driver Display Model (WDDM) is finished presenting the frame on the screen. Stalling the CPU thread until the frame is finished being presented is not efficient so to avoid the stall, we simply render to a different (front) buffer. Since the depth buffer is not used by WDDM, we don’t need to create multiple depth buffers!"

	Note: You cannot use this trick if you need GBuffer Depth information!
	
	*/

	D3D12_HEAP_PROPERTIES depthBuffHeapBufferDesc; //describes heap type
	depthBuffHeapBufferDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
	depthBuffHeapBufferDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthBuffHeapBufferDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	depthBuffHeapBufferDesc.CreationNodeMask = 1;
	depthBuffHeapBufferDesc.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC depthBufferDesc; //describes what is placed in heap
  	depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  	depthBufferDesc.Alignment = 0;
  	depthBufferDesc.DepthOrArraySize = 1;
  	depthBufferDesc.MipLevels = 0;
  	depthBufferDesc.Format = DXGI_FORMAT_D32_FLOAT;
  	depthBufferDesc.SampleDesc.Count = 1;
  	depthBufferDesc.SampleDesc.Quality = 0;
  	depthBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  	depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue;
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // D3D12_DSV_DIMENSION_TEXTURE2DMS for anti aliasing
	depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	u64 dsvDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );

	for( u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye )
	{
		ovrSizei oculusIdealSize = ovr_GetFovTextureSize( oculusSession, (ovrEyeType)dwEye, oculusHMDDesc.DefaultEyeFov[dwEye], 1.0f );
		depthBufferDesc.Width = oculusIdealSize.w;
  		depthBufferDesc.Height = oculusIdealSize.h;
  		//TODO HOW TO COMBINE INTO 1 HEAP!
		if( FAILED( device->CreateCommittedResource( &depthBuffHeapBufferDesc, D3D12_HEAP_FLAG_NONE, &depthBufferDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS( &depthStencilBuffers[dwEye] ) ) ) )
		{
			logError( "Failed to allocate depth buffer!\n" );
			CloseProgram();
			return false;
		}
#if MAIN_DEBUG
		depthStencilBuffers[dwEye]->SetName(L"Eye Depth/Stencil Default Heap");
#endif
		eyeDSVHandle[dwEye] = dsvHandle;
		device->CreateDepthStencilView( depthStencilBuffers[dwEye], &depthStencilViewDesc, dsvHandle );
		dsvHandle.ptr = (u64)dsvHandle.ptr + dsvDescriptorSize;
	}
	return true;
}

//idea during the animation process use an upload heap for rendering but when the animation ends, transfer it to a default heap and use that for the rendering.
inline 
void InitSRVUploadHeap( u32 dwGPUNumber, u32 dwVisibleGPUMask ) //we conly need just an upload heap since we will be changing it every frame
{
#if MAIN_DEBUG
	assert( dwGPUNumber != 0 );
	assert( dwVisibleGPUMask && dwGPUNumber != 0 );
#endif

	D3D12_HEAP_PROPERTIES heapBufferDesc; //describes heap type
	heapBufferDesc.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapBufferDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN; //todo
	heapBufferDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN; //todo
	heapBufferDesc.CreationNodeMask = dwGPUNumber;
	heapBufferDesc.VisibleNodeMask = dwVisibleGPUMask; //todo

	const u64 qwFrameSize = (handBonesCount * sizeof( Mat4f ));

	//todo
	D3D12_RESOURCE_DESC resourceBufferDesc; //describes what is placed in heap
  	resourceBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  	resourceBufferDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; //0;
  	resourceBufferDesc.Width = qwFrameSize;
  	resourceBufferDesc.Height = 1;
  	resourceBufferDesc.DepthOrArraySize = 1;
  	resourceBufferDesc.MipLevels = 1;
  	resourceBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  	resourceBufferDesc.SampleDesc.Count = 1;
  	resourceBufferDesc.SampleDesc.Quality = 0;
  	resourceBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  	resourceBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE; //D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE

	D3D12_RESOURCE_ALLOCATION_INFO allocInfo = device->GetResourceAllocationInfo( dwVisibleGPUMask, 1, &resourceBufferDesc );
	u64 qwNumFullAlignments = qwFrameSize / allocInfo.Alignment;
	const u64 qwAlignedFrameSize = (qwNumFullAlignments * allocInfo.Alignment) + ((qwFrameSize % allocInfo.Alignment) > 0 ? allocInfo.Alignment : 0);
	resourceBufferDesc.Width = qwAlignedFrameSize; //allocations fail if we don't take up the entire page :/
	const u64 qwHeapSize = qwAlignedFrameSize * oculusNUM_FRAMES * ovrHand_Count; //TODO THIS MAY BE WRONG AS each resource may need to be 65536 aligned, then if so increase the max bone count?

	D3D12_HEAP_DESC boneHeapDesc;
	boneHeapDesc.SizeInBytes = qwHeapSize;
	boneHeapDesc.Properties = heapBufferDesc;
	boneHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; //64KB heap alignment, SizeInBytes should be a multiple of the heap alignment. is 64KB here 65536
	boneHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS; //D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

	if( FAILED( device->CreateHeap( &boneHeapDesc, IID_PPV_ARGS(&pSrvBoneHeap) ) ) )
	{
		return;
	}
#if MAIN_DEBUG
	pSrvBoneHeap->SetName( L"SRV Bone Buffer Upload Resource Heap" );
#endif

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = oculusNUM_FRAMES*ovrHand_Count; 
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = dwGPUNumber;

	if( FAILED( device->CreateDescriptorHeap( &srvHeapDesc, IID_PPV_ARGS( &srvDescriptorHeap ) ) ) )
	{
		return;
	}
#if MAIN_DEBUG
	srvDescriptorHeap->SetName( L"SRV Bone Buffer Descriptor Heap" );
#endif

	srvDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	//final state D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	//can we just transistion the heap/resoyrce a bunch between states for cheap?

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,1,2,3);
	srvDesc.Buffer.FirstElement = 0; //index
#if ARRAY_IN_STRUCTURED_BUFFER
	srvDesc.Buffer.NumElements = 1;
	srvDesc.Buffer.StructureByteStride = MAX_BONES * sizeof( Mat4f );
#else
	srvDesc.Buffer.NumElements = MAX_BONES;
	srvDesc.Buffer.StructureByteStride = sizeof( Mat4f );
#endif
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE; //D3D12_BUFFER_SRV_FLAG_RAW (I believe for byte-addressable buffers)

  	//verify that we are using the advanced model!
  	for( u32 dwFrame = 0; dwFrame < oculusNUM_FRAMES; ++dwFrame )
  	{
  		for( u32 dwHand = 0; dwHand < ovrHand_Count; ++dwHand )
  		{
  			//can i set state to D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE?
  			//D3D12_RESOURCE_STATE_COMMON
  			//D3D12_RESOURCE_STATE_GENERIC_READ
			HRESULT allocres = device->CreatePlacedResource( pSrvBoneHeap, (dwFrame*ovrHand_Count + dwHand)*qwAlignedFrameSize, &resourceBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&boneBuffer[dwFrame][dwHand]) );
#if MAIN_DEBUG
			PrintDirectXErrorCode(allocres);
			boneBuffer[dwFrame][dwHand]->SetName(L"Bone Buffer");
#endif
			//sets the descriptor to point to the resource
			device->CreateShaderResourceView( boneBuffer[dwFrame][dwHand],&srvDesc,srvHandle); //used in descriptor table (can I do without this function and just set a struct? like vertex and index buffer views?)
			srvHandle.ptr = (u64)srvHandle.ptr + srvDescriptorSize;
		}
	}
}


//verify if it is even efficent for a VB/IB to start at a not 65536 alignment
inline
void UploadModels( u32 dwGPUNumber, u32 dwVisibleGPUMask )
{
	//https://zhangdoa.com/posts/walking-through-the-heap-properties-in-directx-12
	//https://asawicki.info/news_1726_secrets_of_direct3d_12_resource_alignment
	//https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_heap_tier#D3D12_RESOURCE_HEAP_TIER_1
	// we only allow buffers to stay within the first tier heap tier
	D3D12_HEAP_PROPERTIES heapBufferDesc; //describes heap type
	heapBufferDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapBufferDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN; //potentially D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE since we transfer from Upload heap
	heapBufferDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapBufferDesc.CreationNodeMask = dwGPUNumber;
	heapBufferDesc.VisibleNodeMask = dwVisibleGPUMask;

	const u64 qwModelSize = sizeof(planeVertices) + sizeof(planeIndices) + sizeof(cubeVertices) + sizeof(cubeIndicies) + sizeof(handVertices) + sizeof(handIndices);

	D3D12_RESOURCE_DESC resourceBufferDesc; //describes what is placed in heap
  	resourceBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  	resourceBufferDesc.Alignment = 0;
  	resourceBufferDesc.Width = qwModelSize;
  	resourceBufferDesc.Height = 1;
  	resourceBufferDesc.DepthOrArraySize = 1;
  	resourceBufferDesc.MipLevels = 1;
  	resourceBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  	resourceBufferDesc.SampleDesc.Count = 1;
  	resourceBufferDesc.SampleDesc.Quality = 0;
  	resourceBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  	resourceBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE; //D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE

	D3D12_RESOURCE_ALLOCATION_INFO allocInfo = device->GetResourceAllocationInfo( dwVisibleGPUMask, 1, &resourceBufferDesc );
	u64 qwNumFullAlignments = qwModelSize / allocInfo.Alignment;
	const u64 qwAlignedModelSize = (qwNumFullAlignments * allocInfo.Alignment) + ((qwModelSize % allocInfo.Alignment) > 0 ? allocInfo.Alignment : 0);
	const u64 qwHeapSize = qwAlignedModelSize;


	D3D12_HEAP_DESC modelHeapDesc;
	modelHeapDesc.SizeInBytes = qwHeapSize;
	modelHeapDesc.Properties = heapBufferDesc;
	modelHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; //64KB heap alignment, SizeInBytes should be a multiple of the heap alignment. is 64KB here 65536
	modelHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS; //D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

	device->CreateHeap( &modelHeapDesc, IID_PPV_ARGS(&pModelDefaultHeap) );
#if MAIN_DEBUG
	pModelDefaultHeap->SetName( L"Model Buffer Default Resource Heap" );
#endif

	modelHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;

	device->CreateHeap( &modelHeapDesc, IID_PPV_ARGS(&pModelUploadHeap) );
#if MAIN_DEBUG
	pModelUploadHeap->SetName( L"Model Buffer Upload Resource Heap" );
#endif


  	//verify that we are using the advanced model!
	device->CreatePlacedResource( pModelUploadHeap, 0, &resourceBufferDesc,D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer) );
	device->CreatePlacedResource( pModelDefaultHeap, 0, &resourceBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&defaultBuffer) );

    //upload to upload heap (TODO is there a penalty from crossing a buffer alignment boundary with mesh data?)
    //TODO is there a penatly for not having meshes at an alignment or their own resouce?
    u8* pUploadBufferData;
    if( FAILED( uploadBuffer->Map( 0, nullptr, (void**) &pUploadBufferData ) ) )
    {
        return;
    }
    memcpy(pUploadBufferData,planeVertices,sizeof(planeVertices));
    memcpy(pUploadBufferData+sizeof(planeVertices),planeIndices,sizeof(planeIndices));
    memcpy(pUploadBufferData+sizeof(planeVertices)+sizeof(planeIndices),cubeVertices,sizeof(cubeVertices));
    memcpy(pUploadBufferData+sizeof(planeVertices)+sizeof(planeIndices)+sizeof(cubeVertices),cubeIndicies,sizeof(cubeIndicies));
    memcpy(pUploadBufferData+sizeof(planeVertices)+sizeof(planeIndices)+sizeof(cubeVertices)+sizeof(cubeIndicies),handVertices,sizeof(handVertices));
    memcpy(pUploadBufferData+sizeof(planeVertices)+sizeof(planeIndices)+sizeof(cubeVertices)+sizeof(cubeIndicies)+sizeof(handVertices),handIndices,sizeof(handIndices));
    uploadBuffer->Unmap( 0, nullptr );

	commandLists[ovrEye_Count]->CopyResource( defaultBuffer, uploadBuffer );
	//commandLists[ovrEye_Count]->CopyBufferRegion( defaultBuffer, 0, uploadBuffer, 0, sizeof(planeVertices)+sizeof(planeIndices)+sizeof(cubeVertices)+sizeof(cubeIndicies) );

	D3D12_RESOURCE_BARRIER defaultHeapUploadToReadBarrier;
    defaultHeapUploadToReadBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    defaultHeapUploadToReadBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    defaultHeapUploadToReadBarrier.Transition.pResource = defaultBuffer;
   	defaultHeapUploadToReadBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    defaultHeapUploadToReadBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    defaultHeapUploadToReadBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    commandLists[ovrEye_Count]->ResourceBarrier( 1, &defaultHeapUploadToReadBarrier );

    //does this apply in my case https://twitter.com/MyNameIsMJP/status/1574431011579928580 ?
    planeVertexBufferView.BufferLocation = defaultBuffer->GetGPUVirtualAddress();
    planeVertexBufferView.StrideInBytes = 3*sizeof(f32) + 3*sizeof(f32) + 4*sizeof(f32); //size of s single vertex
    planeVertexBufferView.SizeInBytes = sizeof(planeVertices);

	planeIndexBufferView.BufferLocation = planeVertexBufferView.BufferLocation + sizeof(planeVertices);
    planeIndexBufferView.SizeInBytes = sizeof(planeIndices);
    planeIndexBufferView.Format = DXGI_FORMAT_R32_UINT; 

    cubeVertexBufferView.BufferLocation = planeIndexBufferView.BufferLocation+sizeof(planeIndices);
    cubeVertexBufferView.StrideInBytes = 3*sizeof(f32) + 3*sizeof(f32) + 4*sizeof(f32); //size of s single vertex
    cubeVertexBufferView.SizeInBytes = sizeof(cubeVertices);

	cubeIndexBufferView.BufferLocation = cubeVertexBufferView.BufferLocation+sizeof(cubeVertices);
    cubeIndexBufferView.SizeInBytes = sizeof(cubeIndicies);
    cubeIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

    handVertexBufferView.BufferLocation = cubeIndexBufferView.BufferLocation+sizeof(cubeIndicies);
    handVertexBufferView.StrideInBytes = 3*sizeof(f32) + 3*sizeof(f32) + 4*sizeof(u32) + 4*sizeof(f32) + 4*sizeof(f32); //size of s single vertex
    handVertexBufferView.SizeInBytes = sizeof(handVertices);

	handIndexBufferView.BufferLocation = handVertexBufferView.BufferLocation+sizeof(handVertices);
    handIndexBufferView.SizeInBytes = sizeof(handIndices);
    handIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

//when using multiple memory srcs for input for rendering the following will be the main slot
#define MAIN_VB_SLOT 0

#define VERTEX_CB_ROOT_SLOT 0
#define PIXEL_CB_ROOT_SLOT 1
#define VERTEX_SB_ROOT_SLOT 2
//are structured buffers best here, also are they in SRV? or what if so
// do i need to create a heap for then and store a descriptor for that in a descriptor table
// then store that table in the root signature? https://www.gamedev.net/forums/topic/708895-structured-buffers-in-dx12/
//https://www.gamedev.net/forums/topic/624529-structured-buffers-vs-constant-buffers/

inline 
bool InitPipelineStates()
{
	//vertex shader constants
	D3D12_ROOT_CONSTANTS cbVertDesc;
	cbVertDesc.ShaderRegister = 0;
	cbVertDesc.RegisterSpace = 0;
	                            //float4x4  //float3x3 (a float of padding between each row)
	cbVertDesc.Num32BitValues = ( 4 * 4 ) + ( ( ( 4 * 2 ) + 3 ) );

	D3D12_ROOT_CONSTANTS cbPixelDesc;
	cbPixelDesc.ShaderRegister = 1; //can this be 1 cause in pixel shader not vertex shader, but what about mem being shared between shaders?
	cbPixelDesc.RegisterSpace = 0;
	                            //float4 and float3
	cbPixelDesc.Num32BitValues = 4 + 3;

	D3D12_ROOT_PARAMETER rootParams[3];
	rootParams[VERTEX_CB_ROOT_SLOT].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParams[VERTEX_CB_ROOT_SLOT].Constants = cbVertDesc;
	rootParams[VERTEX_CB_ROOT_SLOT].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	rootParams[PIXEL_CB_ROOT_SLOT].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParams[PIXEL_CB_ROOT_SLOT].Constants = cbPixelDesc;
	rootParams[PIXEL_CB_ROOT_SLOT].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	//TODO can i set a 32BIT constant for an SRV?
	rootParams[VERTEX_SB_ROOT_SLOT].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParams[VERTEX_SB_ROOT_SLOT].Descriptor.ShaderRegister = 0;
	rootParams[VERTEX_SB_ROOT_SLOT].Descriptor.RegisterSpace = 0;
	rootParams[VERTEX_SB_ROOT_SLOT].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	//D3D12_VERSIONED_ROOT_SIGNATURE_DESC
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.NumParameters = 2;
	rootSignatureDesc.pParameters = rootParams;
	rootSignatureDesc.NumStaticSamplers = 0;
	rootSignatureDesc.pStaticSamplers = nullptr;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT  | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS  | D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS; //D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS

	ID3DBlob* serializedRootSignature;
	if( FAILED( D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSignature, nullptr ) ) )
	{
		logError( "Failed to serialize root signature!\n" );
		return false;
	}

	if( FAILED( device->CreateRootSignature(0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( &rootSignature ) ) ) )
	{
		logError( "Failed to create root signature!\n" );
		return false;
	}

	//create root signature for skinned meshes
	rootSignatureDesc.NumParameters = 3;
	ID3DBlob* serializedSkinnedRootSignature;
	if( FAILED( D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedSkinnedRootSignature, nullptr ) ) )
	{
		logError( "Failed to serialize skinned root signature!\n" );
		return false;
	}

	if( FAILED( device->CreateRootSignature(0, serializedSkinnedRootSignature->GetBufferPointer(), serializedSkinnedRootSignature->GetBufferSize(), IID_PPV_ARGS( &skinnedRootSignature ) ) ) )
	{
		logError( "Failed to create skinned root signature!\n" );
		return false;
	}
	//todo can i free serializedRootSignature here?

	//this describes the vertex inut layout (if we were doing instancing this can change to allow per instance data)

	D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, MAIN_VB_SLOT, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, MAIN_VB_SLOT, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, MAIN_VB_SLOT, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	DXGI_SAMPLE_DESC sampleDesc;
	sampleDesc.Count = dwSampleRate;
	sampleDesc.Quality = 0; //TODO if msaa is added


	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc;
	inputLayoutDesc.pInputElementDescs = inputLayout;
	inputLayoutDesc.NumElements = 3;

	D3D12_SHADER_BYTECODE vertexShaderBytecode;
	vertexShaderBytecode.pShaderBytecode = vertexShaderBlob;
	vertexShaderBytecode.BytecodeLength = sizeof(vertexShaderBlob);

	D3D12_SHADER_BYTECODE pixelShaderBytecode;
	pixelShaderBytecode.pShaderBytecode = pixelShaderBlob;
	pixelShaderBytecode.BytecodeLength = sizeof(pixelShaderBlob);

	D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc;
	renderTargetBlendDesc.BlendEnable = 0;
	renderTargetBlendDesc.LogicOpEnable = 0;
	renderTargetBlendDesc.SrcBlend = D3D12_BLEND_ONE;
	renderTargetBlendDesc.DestBlend = D3D12_BLEND_ZERO;
	renderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	renderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	renderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	renderTargetBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_BLEND_DESC pipelineBlendState;
	pipelineBlendState.AlphaToCoverageEnable = 0;
	pipelineBlendState.IndependentBlendEnable = 0;
	for( u32 dwRenderTarget = 0; dwRenderTarget < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++dwRenderTarget )
	{
		pipelineBlendState.RenderTarget[ dwRenderTarget ] = renderTargetBlendDesc;
	}

	D3D12_RASTERIZER_DESC pipelineRasterizationSettings;
	pipelineRasterizationSettings.FillMode = D3D12_FILL_MODE_SOLID;
	pipelineRasterizationSettings.CullMode = D3D12_CULL_MODE_BACK;
	pipelineRasterizationSettings.FrontCounterClockwise = 0;
	pipelineRasterizationSettings.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	pipelineRasterizationSettings.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	pipelineRasterizationSettings.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	pipelineRasterizationSettings.DepthClipEnable = 1;
	pipelineRasterizationSettings.MultisampleEnable = 0;
	pipelineRasterizationSettings.AntialiasedLineEnable = 0;
	pipelineRasterizationSettings.ForcedSampleCount = 0;
	pipelineRasterizationSettings.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	D3D12_DEPTH_STENCILOP_DESC frontFaceDesc;
	frontFaceDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	frontFaceDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	frontFaceDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	frontFaceDesc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_DEPTH_STENCILOP_DESC backFaceDesc;
	backFaceDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	backFaceDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	backFaceDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	backFaceDesc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_DEPTH_STENCIL_DESC pipelineDepthStencilState;
	pipelineDepthStencilState.DepthEnable = 1;
	pipelineDepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pipelineDepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pipelineDepthStencilState.StencilEnable = 0;
	pipelineDepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	pipelineDepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	pipelineDepthStencilState.FrontFace = frontFaceDesc;
	pipelineDepthStencilState.BackFace = backFaceDesc;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc;
	pipelineDesc.pRootSignature = rootSignature; //why is this even here if we are going to set it in the command list?
	pipelineDesc.VS = vertexShaderBytecode;
	pipelineDesc.PS = pixelShaderBytecode;
	pipelineDesc.DS = {};
	pipelineDesc.HS = {};
	pipelineDesc.GS = {};
	pipelineDesc.StreamOutput = {};
	pipelineDesc.BlendState = pipelineBlendState;
	pipelineDesc.SampleMask = 0xffffffff;
	pipelineDesc.RasterizerState = pipelineRasterizationSettings;
	pipelineDesc.DepthStencilState = pipelineDepthStencilState;
	pipelineDesc.InputLayout = inputLayoutDesc;
	pipelineDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED ;
	pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineDesc.NumRenderTargets = 1;
	pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	for( u32 dwRenderTargetFormat = 1; dwRenderTargetFormat < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++dwRenderTargetFormat )
	{
		pipelineDesc.RTVFormats[dwRenderTargetFormat] = DXGI_FORMAT_UNKNOWN;
	}
	pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipelineDesc.SampleDesc = sampleDesc;
	pipelineDesc.NodeMask = 0;
	pipelineDesc.CachedPSO = {};
	pipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE; //set in debug mode for embedded graphics

	if( FAILED( device->CreateGraphicsPipelineState( &pipelineDesc, IID_PPV_ARGS( &pipelineStateObject ) ) ) )
	{
		logError( "Failed to create pipeline state object!\n" );
		return false;
	}

	D3D12_INPUT_ELEMENT_DESC inputLayoutSkinned[] =
	{
		{ "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, MAIN_VB_SLOT, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, MAIN_VB_SLOT, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "JOINT", 0, DXGI_FORMAT_R32G32B32A32_UINT, MAIN_VB_SLOT, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "WEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, MAIN_VB_SLOT, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, MAIN_VB_SLOT, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_INPUT_LAYOUT_DESC inputLayoutSkinndedDesc;
	inputLayoutSkinndedDesc.pInputElementDescs = inputLayoutSkinned;
	inputLayoutSkinndedDesc.NumElements = 5;

	pipelineDesc.pRootSignature = skinnedRootSignature;

	D3D12_SHADER_BYTECODE vertexShaderSkinnedBytecode;
	vertexShaderSkinnedBytecode.pShaderBytecode = vertexShaderSkinnedBlob;
	vertexShaderSkinnedBytecode.BytecodeLength = sizeof(vertexShaderSkinnedBlob);

	pipelineDesc.VS = vertexShaderSkinnedBytecode;
	pipelineDesc.InputLayout = inputLayoutSkinndedDesc;

	if( FAILED( device->CreateGraphicsPipelineState( &pipelineDesc, IID_PPV_ARGS( &skinnedPipelineStateObject ) ) ) )
	{
		logError( "Failed to create skinned pipeline state object!\n" );
		return false;
	}

	return true;
}

inline
u8 InitDirectX12()
{
	//init directx 12

	//can this factory be downgraded
	IDXGIFactory4 *dxgiFactory;
	if( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &dxgiFactory ) ) ) )
	{
		logError( "Error creating DXGI factory\n" );  
		return 1;
	}

	//Find the gpu with the headset attachted to it
	IDXGIAdapter* adapter = nullptr;
    for(u32 dwAdapter = 0; dxgiFactory->EnumAdapters(dwAdapter, &adapter) != DXGI_ERROR_NOT_FOUND; ++dwAdapter )
    {
        DXGI_ADAPTER_DESC adapterDesc;
        adapter->GetDesc(&adapterDesc);
        //reinterpret_cast<LUID*>(&oculusGLuid)
        if (memcmp(&adapterDesc.AdapterLuid, &oculusGLuid, sizeof(LUID)) == 0)
        {
        	break;
        }
        adapter->Release();
    }

	//actually retrieve the device interface to the adapter
	if( FAILED( D3D12CreateDevice( adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS( &device ) ) ) )
	{
		logError( "Error could not open directx 12 supporting GPU or find GPU with Headset Attached to it!\n" );  
		return 1;
	}
	adapter->Release();

	//todo get gpu preference device for window rendering (could be separate than VR but not always, may have to duplicate models in both gpu memories or transfer over screen texture between them)

#if MAIN_DEBUG
	//for getting errors from directx when debugging
	if( SUCCEEDED( device->QueryInterface( IID_PPV_ARGS( &pIQueue ) ) ) )
	{
		pIQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pIQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pIQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
        

        // Suppress whole categories of messages
        //D3D12_MESSAGE_CATEGORY Categories[] = {};

        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO
        };
 
        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };
 
        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;
 
        if( FAILED( pIQueue->PushStorageFilter( &NewFilter ) ) )
        {
        	logError( "Detected device creation problem!\n" );  
        	return 1;
        }
	}
#endif

	commandQueue = InitDirectCommandQueue( device );
	if( !commandQueue )
	{
		logError( "Failed to create command queue!\n" ); 
		return 1;
	}


	{
		ovrTextureSwapChainDesc eyeSwapchainColorTextureDesc;
		eyeSwapchainColorTextureDesc.Type = ovrTexture_2D;
		eyeSwapchainColorTextureDesc.Format =  OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
		eyeSwapchainColorTextureDesc.ArraySize = 1;
		eyeSwapchainColorTextureDesc.MipLevels = 1;
		eyeSwapchainColorTextureDesc.SampleCount = dwSampleRate;
		eyeSwapchainColorTextureDesc.StaticImage = ovrFalse;
		eyeSwapchainColorTextureDesc.MiscFlags = ovrTextureMisc_DX_Typeless | ovrTextureMisc_AutoGenerateMips;
		eyeSwapchainColorTextureDesc.BindFlags = ovrTextureBind_DX_RenderTarget;

		for( u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye )
		{
			ovrSizei oculusIdealSize = ovr_GetFovTextureSize( oculusSession, (ovrEyeType)dwEye, oculusHMDDesc.DefaultEyeFov[dwEye], 1.0f );
			
			//setup viewport for where to write output for image
			oculusEyeRenderViewport[dwEye].Pos.x = 0;
			oculusEyeRenderViewport[dwEye].Pos.y = 0;
			oculusEyeRenderViewport[dwEye].Size = oculusIdealSize;

			EyeViewports[dwEye].TopLeftX = 0;
			EyeViewports[dwEye].TopLeftY = 0;
			EyeViewports[dwEye].Width = (f32)oculusIdealSize.w;
			EyeViewports[dwEye].Height = (f32)oculusIdealSize.h;
			EyeViewports[dwEye].MinDepth = 0.0f;
			EyeViewports[dwEye].MaxDepth = 1.0f;

    		EyeScissorRects[dwEye].left = 0;
    		EyeScissorRects[dwEye].top = 0;
    		EyeScissorRects[dwEye].right = oculusIdealSize.w;
    		EyeScissorRects[dwEye].bottom = oculusIdealSize.h;

			//TODO create eye swap chains (is it possible to create both at once by upping thr array size number?)
			eyeSwapchainColorTextureDesc.Width = oculusIdealSize.w;
			eyeSwapchainColorTextureDesc.Height = oculusIdealSize.h;
	
			if( ovr_CreateTextureSwapChainDX( oculusSession, commandQueue, &eyeSwapchainColorTextureDesc, &oculusEyeSwapChains[dwEye] ) < 0 )
			{
				logError( "Failed to create swap chain texture for eye!" );
				return 1;
			}
		}
	}


	//does oculusNUM_FRAMES change between head sets?
	// or there a constant defined in libOVR so I don't have to do this
	ovr_GetTextureSwapChainLength( oculusSession, oculusEyeSwapChains[0] , (s32*)&oculusNUM_FRAMES);

	commandAllocators = (ID3D12CommandAllocator**)malloc( (oculusNUM_FRAMES*ovrEye_Count*(sizeof(ID3D12CommandAllocator*) + sizeof(ID3D12Resource*))) + sizeof(ID3D12CommandAllocator*) );
	oculusEyeBackBuffers = (ID3D12Resource**)(commandAllocators + (oculusNUM_FRAMES*ovrEye_Count) + 1);

#if MAIN_DEBUG
	s32 otherTextureCount;
	for( u32 dwEye = 1; dwEye < ovrEye_Count; ++dwEye )
	{
		ovr_GetTextureSwapChainLength( oculusSession, oculusEyeSwapChains[dwEye] , &otherTextureCount);
		assert( otherTextureCount == oculusNUM_FRAMES );
	}
#endif

	rtvDescriptorHeap = InitRenderTargetDescriptorHeap( device, oculusNUM_FRAMES*ovrEye_Count ); //change amt for debug mode
	if( !rtvDescriptorHeap )
	{
		logError( "Failed to create render target descriptor heap!\n" ); 
		return 1;
	}

	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

	{
		//Populate descriptor heap
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	
		D3D12_RENDER_TARGET_VIEW_DESC eyeRTVDesc;
    	eyeRTVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    	eyeRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; //D3D12_RTV_DIMENSION_TEXTURE2DMS for MSAA
    	eyeRTVDesc.Texture2D.MipSlice = 0;
    	eyeRTVDesc.Texture2D.PlaneSlice = 0;
    	//eyeRTVDesc.Texture2DMS.UnusedField_NothingToDefine = 0; //for MSAA
	
		for(u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye)
		{
			eyeStartingRTVHandle[dwEye] = rtvHandle;
	
			for( u32 dwIdx = 0; dwIdx < (u32)oculusNUM_FRAMES; ++dwIdx )
			{
				if( ovr_GetTextureSwapChainBufferDX( oculusSession, oculusEyeSwapChains[dwEye], dwIdx, IID_PPV_ARGS(&oculusEyeBackBuffers[(dwEye*oculusNUM_FRAMES) + dwIdx])) < 0 )
				{
					logError( "Failed to get rtv handle!\n" ); 
					return 1;
				}
#if MAIN_DEBUG
				oculusEyeBackBuffers[(dwEye*oculusNUM_FRAMES) + dwIdx]->SetName(L"Eye Swap Chain Texture");
#endif
				device->CreateRenderTargetView( oculusEyeBackBuffers[(dwEye*oculusNUM_FRAMES) + dwIdx], &eyeRTVDesc, rtvHandle );
				rtvHandle.ptr = (u64)rtvHandle.ptr + rtvDescriptorSize;
			}
		}
	}

	dsDescriptorHeap = InitDepthStencilDescriptorHeap( device, ovrEye_Count );
	if( !dsDescriptorHeap )
	{
		logError( "Failed to create depth buffer descriptor heap!\n" );
		return 1;
	}

	if( !CreateDepthStencilBuffer() )
	{
		return 1;
	}


	InitSRVUploadHeap(0x1,0x1);

/*
	//TODO change to just using a root descriptor in the root signature
	srvDescriptorHeap = InitSRVDescriptorHeap();
		if( !srvDescriptorHeap )
	{
		logError( "Failed to create SRV descriptor heap!\n" );
		return 1;
	}
*/

	for( u32 dwIdx = 0; dwIdx < (u32)(oculusNUM_FRAMES*ovrEye_Count+1); ++dwIdx )
	{
		if( FAILED( device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &commandAllocators[dwIdx] ) ) ) )
		{
			logError( "Failed to create command allocator!\n" );
			return 1;
		}
	}

	ovr_GetTextureSwapChainCurrentIndex(oculusSession, oculusEyeSwapChains[0], (s32*)&oculusCurrentFrameIdx);
#if MAIN_DEBUG
	s32 otherSwapChainIndex;
	for( u32 dwEye = 1; dwEye < ovrEye_Count; ++dwEye )
	{
		ovr_GetTextureSwapChainCurrentIndex(oculusSession, oculusEyeSwapChains[dwEye], &otherSwapChainIndex);
		assert( otherSwapChainIndex == oculusCurrentFrameIdx );
	}
#endif

	//create a command list for each eye plus 1 for model uploading (in the future could all be split across threads)
	for( u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye )
	{
		if( FAILED( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[(dwEye*oculusNUM_FRAMES)+oculusCurrentFrameIdx], NULL, IID_PPV_ARGS( &commandLists[dwEye] ) ) ) )
		{
			logError( "Failed to create Command list (it will change which allocator it allocates commands into every frame)!\n" );
			return 1;
		}
#if MAIN_DEBUG
		commandLists[dwEye]->SetName(L"Eye Command List");
#endif
		if( FAILED( commandLists[dwEye]->Close() ) )
		{
			logError( "Command list failed to close, go through debug layer to see what command failed!\n" );
			return 1;
		}
	}
	if( FAILED( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[ovrEye_Count*oculusNUM_FRAMES], NULL, IID_PPV_ARGS( &commandLists[ovrEye_Count] ) ) ) )
	{
		logError( "Failed to create Command list (it will change which allocator it allocates commands into every frame)!\n" );
		return 1;
	}
#if MAIN_DEBUG
	commandLists[ovrEye_Count]->SetName(L"Streaming Command List");
#endif

	if( FAILED( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &streamingFence ) ) ) )
	{
		logError( "Failed to create GPU/CPU fence!\n" );
		return false;
	}

	currStreamingFenceValue = 0;
	streamingFenceValue = 0;

	fenceEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	if( !fenceEvent )
	{
		logError( "Failed to create fence event!\n" );
		return false;
	}

	UploadModels(0x1,0x1); //upload meshes to GPU 1

	//finish up streaming command list
	if( FAILED( commandLists[ovrEye_Count]->Close() ) )
	{
		logError( "Command list failed to close, go through debug layer to see what command failed!\n" );
		return 1;
	}

	//execute streaming command list
	ID3D12CommandList* ppCommandLists[] = { commandLists[ovrEye_Count] };
    commandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

    FlushStreamingCommandQueue();
    uploadBuffer->Release();
	pModelUploadHeap->Release();

	if( !InitPipelineStates() )
	{
		return false;
	}

	return 0;
}


//change release to WinMainCRTStartup

struct ControllerState
{
	f32 m_fSideTrigger;
	f32 m_fFrontTrigger;
};


void DrawScene( f32 deltaTime ) //todo change to f64 for higher precision time steps 
{
	ovrSessionStatus oculusSessionStatus;
    ovr_GetSessionStatus( oculusSession, &oculusSessionStatus );
    if( oculusSessionStatus.ShouldQuit )
    {
    	CloseProgram();
    	return;
    }
    if( oculusSessionStatus.ShouldRecenter )
    {
    	ovr_RecenterTrackingOrigin( oculusSession );
    }
    if( !oculusSessionStatus.HasInputFocus )
    {
    	Pause();
    	deltaTime = 0.0f;
    }
    else
    {
    	UnPause(); //todo have the user unpause here instead
    }

    //TODO while paused grey tint the world
    if( oculusSessionStatus.IsVisible )
    {
    	if( ovr_WaitToBeginFrame( oculusSession, oculusFrameCount ) < 0 )
    	{
#if MAIN_DEBUG
    		//TODO change to a retry create head set, maybe?
    		printf("wait to begin failed\n");
#endif
    		CloseProgram();
    		return;
    	}

    	//predict when the current frame will be displayed (predicted time for frame oculusFrameCount)
    	f64 fOculusFrameTiming = ovr_GetPredictedDisplayTime( oculusSession, oculusFrameCount ); 

    	if( ovr_BeginFrame( oculusSession, oculusFrameCount ) < 0 )
    	{
#if MAIN_DEBUG
    		//TODO change to a retry create head set, maybe?
    		printf("begin failed\n");
#endif
    		CloseProgram();
    		return;
    	}

    	ovr_GetTextureSwapChainCurrentIndex(oculusSession, oculusEyeSwapChains[0], (s32*)&oculusCurrentFrameIdx); //I don't think this will ever be out of sync between swap chains...

    	//get head and hand tracked state
    	//https://developer.oculus.com/documentation/native/pc/dg-input-touch-poses/
    	//https://developer.oculus.com/documentation/native/pc/dg-input-touch/
    	//https://developer.oculus.com/documentation/native/pc/dg-input-touch-buttons/
    	//https://developer.oculus.com/documentation/native/pc/dg-input-touch-touch/
    	ovrTrackingState oculusTrackState = ovr_GetTrackingState( oculusSession, fOculusFrameTiming, ovrFalse );
    	//ovrTrackerPose oculusTrackerPose = ovr_GetTrackerPose( oculusSession, 0); //is this for getting the world poses for the old 2 cameras that would watch the scene? (outside in tracking for before rift s oculus, or are these little extra things you would wear for tracking? or what the heck does the index return?)
    	//TODO vision tracking?

		ovrInputState oculusControllerInputState;


    	//todo HandleMovement of player here

    	//https://developer.oculus.com/documentation/native/pc/dg-sensor/
    	//https://developer.oculus.com/documentation/native/pc/dg-render/
    	//https://developer.oculus.com/documentation/native/pc/dg-render-advanced/
    	//https://developer.oculus.com/documentation/native/pc/dg-vr-focus/

    	//will this change? why can't i just do this once or when the HMD characterics change?
    	ovrEyeRenderDesc oculusEyeRenderDesc[ovrEye_Count];
    	ovrPosef EyeRenderPose[ovrEye_Count];
    	ovrPosef HmdToEyePose[ovrEye_Count]; //Transform of eye from the HMD center (but how to get HMD center position in absolute space?), in meters. (in other words ipd taken into account)
    	for( u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye )
    	{
    		oculusEyeRenderDesc[dwEye] = ovr_GetRenderDesc( oculusSession, (ovrEyeType)dwEye, oculusHMDDesc.DefaultEyeFov[dwEye] );
    		HmdToEyePose[dwEye] = oculusEyeRenderDesc[dwEye].HmdToEyePose;
    	}

    	//converts HMDToEye (ipd for each eye from head set center) to actual world space position's (from origin)
    	//f64 fSensorSampleTime;
    	ovr_CalcEyePoses( oculusTrackState.HeadPose.ThePose, HmdToEyePose, EyeRenderPose ); //TODO verify it works.. and it is accurate and more efficent than ovr_GetEyePoses, is it bad we lose out on fSensorSampleTime or do we even with predicted frame timing? 
    	//ovr_GetEyePoses( oculusSession, oculusFrameCount, ovrTrue, HmdToEyePose, EyeRenderPose, &fSensorSampleTime );
    	//my guess of how ovr_GetEyePoses is implemented is that it calls ovr_GetTrackingState then ovr_CalcEyePoses

    	//todo verify with mouse manipulation of headset view
		Quatf qHor, qVert;
		Vec3f vertAxis = {cosf(rotHor*PI_F/180.0f),0,-sinf(rotHor*PI_F/180.0f)};
		Vec3f horAxis = {0,1,0};
		InitUnitQuatf( &qVert, rotVert, &vertAxis );
		InitUnitQuatf( &qHor, rotHor, &horAxis );
	
		Quatf qRot;
		QuatfMult( &qVert, &qHor, &qRot);

    	Mat4f mTrans;
    	InitTransMat4f( &mTrans, 0, 0, -5); //TODO should this be negative or the view matrix position be negated?
    	Mat4f mRot;
    	Vec3f rotAxis = {0.57735026919f,0.57735026919f,0.57735026919f};
    	static f32 cubeRotAngle = 0;
    	cubeRotAngle += 50.0f*deltaTime;
    	InitRotArbAxisMat4f( &mRot, &rotAxis, cubeRotAngle );
    	Mat4f mCubeModel;
    	Mat4fMult(&mRot, &mTrans, &mCubeModel);

		Mat4f mPlaneModel;
		InitMat4f( &mPlaneModel );

		//you need to fully understand the game to decide whether both controllers need to be present and pause on one missing
		// or allow to keep playing with say one of the 2 hands disconnected
		u8 hwHandPresent[ovrHand_Count];
		Mat4f mHandModel[ovrHand_Count];
		u8 hwHandFlags = 0;
		for( u32 dwHand = 0; dwHand < ovrHand_Count; ++dwHand )
		{
			hwHandPresent[dwHand] = (oculusTrackState.HandStatusFlags[dwHand] & (ovrStatus_OrientationTracked|ovrStatus_PositionTracked)) > 0 ? 1 : 0;
			if( hwHandPresent[dwHand] )
			{
				hwHandFlags |= (1 << dwHand);
				Quatf handQuat;
    			handQuat.w = oculusTrackState.HandPoses[dwHand].ThePose.Orientation.w;
    			handQuat.x = oculusTrackState.HandPoses[dwHand].ThePose.Orientation.x;
    			handQuat.y = oculusTrackState.HandPoses[dwHand].ThePose.Orientation.y;
    			handQuat.z = oculusTrackState.HandPoses[dwHand].ThePose.Orientation.z;
    			
    			Vec3f handPos;
    			handPos.x = oculusTrackState.HandPoses[dwHand].ThePose.Position.x; 
    			handPos.y = oculusTrackState.HandPoses[dwHand].ThePose.Position.y;
    			handPos.z = oculusTrackState.HandPoses[dwHand].ThePose.Position.z;

    			InitModelMat4ByQuatf(&mHandModel[dwHand], &handQuat, &handPos);
			}
		}

		ControllerState handStates[ovrHand_Count];

		//input is the one spot where you actually want decent error handling...
		//like what if the controller dies between getting the pose and sampling the button states
		if (OVR_SUCCESS(ovr_GetInputState(oculusSession, (ovrControllerType)hwHandFlags, &oculusControllerInputState)))
		{
			Mat4f mHandFrameBindBones[ovrHand_Count][handBonesCount];
			if(oculusControllerInputState.Buttons & ovrButton_A)
			{
				//if a is being pressed
			}

			for( u32 dwHand = 0; dwHand < ovrHand_Count; ++dwHand )
			{
				if( hwHandFlags & (1 << dwHand)) //only update hand if it is present
				{

					if(oculusControllerInputState.Touches & ( ovrTouch_RIndexTrigger << ( 8 * dwHand ) ) )
					{
						//if index finger trigger is on right trigger, is this mutually exlcusive with ( ovrTouch_RIndexPointing << ( 8 * dwHand ) )?
					}

					//gesture
					if(oculusControllerInputState.Touches & ( ovrTouch_RIndexPointing << ( 8 * dwHand ) ) )
					{
						//if index finger trigger is being being pointed
					}
					
					if(oculusControllerInputState.Touches & ( ovrTouch_RThumb << ( 8 * dwHand ) ))
					{
						//if thumb is on joystick
					}

					//gesture
					if(oculusControllerInputState.Touches & ( ovrTouch_RThumbUp << ( 8 * dwHand ) ))
					{
						//if thumb is above joystick is this mutually exlcusive with ( ovrTouch_RThumbRest << ( 8 * dwHand ) )?
					}

					if(oculusControllerInputState.Touches & ( ovrTouch_RThumbRest << ( 8 * dwHand ) ))
					{
						//if thumb is on thumb rest being touched (only on quest controllers it seems)
					}
	
					if (oculusControllerInputState.IndexTrigger[dwHand] > 0.15f)
					{
					    // index finger pressed state...
					    handStates[dwHand].m_fFrontTrigger = oculusControllerInputState.IndexTrigger[dwHand];
					}
					else
					{
						handStates[dwHand].m_fFrontTrigger = 0.0f;
					}
	
					if (oculusControllerInputState.HandTrigger[dwHand] > 0.15f)
					{
					    // index finger pressed state...
					    handStates[dwHand].m_fSideTrigger = oculusControllerInputState.HandTrigger[dwHand];
					}
					else
					{
						handStates[dwHand].m_fSideTrigger = 0.0f;
					}
	
					ovrVector2f vThumbStick = oculusControllerInputState.Thumbstick[dwHand];
				
					u32 dwStartingOffset = 0; //needs to be min
					u32 dwNumMats = 0;
										if( handStates[dwHand].m_fSideTrigger != fPrevSideFingerDownAmount[oculusCurrentFrameIdx][dwHand] )
					{
						fPrevSideFingerDownAmount[oculusCurrentFrameIdx][dwHand] = handStates[dwHand].m_fSideTrigger;
						f64 fTotalTime = handInnerAnimTimeStamps[animationInnerKeyframeCount-1] - handInnerAnimTimeStamps[0];
						if( handStates[dwHand].m_fSideTrigger == 0.0 )
						{
							InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans ); 
							for( u32 dwBone = firstInnerBone; dwBone < (firstInnerBone+numInnerChannels); ++dwBone )
							{				
								Mat4f mLocalFrameBone;
								InitModelMat4ByQuatf( &mLocalFrameBone, &handInnerKeyFrames[0][dwBone-firstInnerBone].qRot, &handInnerKeyFrames[0][dwBone-firstInnerBone].vPos );
								Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
								Mat4fMult(&handInvBind[dwBone],&mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
							}
						}
						else if( handStates[dwHand].m_fSideTrigger == 1.0 )
						{
							InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans ); 
							for( u32 dwBone = firstInnerBone; dwBone < (firstInnerBone+numInnerChannels); ++dwBone )
							{				
								Mat4f mLocalFrameBone;
								InitModelMat4ByQuatf( &mLocalFrameBone, &handInnerKeyFrames[animationInnerKeyframeCount-1][dwBone-firstInnerBone].qRot, &handInnerKeyFrames[animationInnerKeyframeCount-1][dwBone-firstInnerBone].vPos );
								Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
								Mat4fMult(&handInvBind[dwBone],&mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
							}
						}
						else
						{
							f64 fCurrAnimationTime = fTotalTime * handStates[dwHand].m_fSideTrigger;
							u32 dwCurrKeyFrame = 1;
							for( ; dwCurrKeyFrame < animationInnerKeyframeCount; ++dwCurrKeyFrame )
							{
								if( fCurrAnimationTime <= handInnerAnimTimeStamps[dwCurrKeyFrame])
								{
									break;
								}
							}
							u32 dwPrevKeyFrame = dwCurrKeyFrame-1;
							f32 fT = (f32)((fCurrAnimationTime - handInnerAnimTimeStamps[dwPrevKeyFrame]) / (handInnerAnimTimeStamps[dwCurrKeyFrame] - handInnerAnimTimeStamps[dwPrevKeyFrame]));
							Quatf rot;
							Vec3f pos;
							InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans ); 
							for( u32 dwBone = firstInnerBone; dwBone < (firstInnerBone+numInnerChannels); ++dwBone )
							{				
								Mat4f mLocalFrameBone;
								QuatfNormLerp(&handInnerKeyFrames[dwCurrKeyFrame][dwBone-firstInnerBone].qRot,&handInnerKeyFrames[dwPrevKeyFrame][dwBone-firstInnerBone].qRot,fT,&rot );
								Vec3fLerp(&handInnerKeyFrames[dwCurrKeyFrame][dwBone-firstInnerBone].vPos,&handInnerKeyFrames[dwPrevKeyFrame][dwBone-firstInnerBone].vPos,fT,&pos);
								InitModelMat4ByQuatf( &mLocalFrameBone, &rot, &pos );
								Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
								Mat4fMult(&handInvBind[dwBone], &mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
							}
						}

						dwStartingOffset = firstInnerBone;
						dwNumMats += numInnerChannels;
					}
					if( handStates[dwHand].m_fFrontTrigger != fPrevIndexFingerDownAmount[oculusCurrentFrameIdx][dwHand] )
					{
						fPrevIndexFingerDownAmount[oculusCurrentFrameIdx][dwHand] = handStates[dwHand].m_fFrontTrigger;
						f64 fTotalTime = handOutterAnimTimeStamps[animationOutterKeyframeCount-1] - handOutterAnimTimeStamps[0];
						if( handStates[dwHand].m_fFrontTrigger == 0.0 )
						{
							InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans ); 
							for( u32 dwBone = firstOutterBone; dwBone < (firstOutterBone+numOutterChannels); ++dwBone )
							{				
								Mat4f mLocalFrameBone;
								InitModelMat4ByQuatf( &mLocalFrameBone, &handOutterKeyFrames[0][dwBone-firstOutterBone].qRot, &handOutterKeyFrames[0][dwBone-firstOutterBone].vPos );
								Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
								Mat4fMult(&handInvBind[dwBone],&mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
							}
						}
						else if( handStates[dwHand].m_fFrontTrigger == 1.0 )
						{
							InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans ); 
							for( u32 dwBone = firstOutterBone; dwBone < (firstOutterBone+numOutterChannels); ++dwBone )
							{				
								Mat4f mLocalFrameBone;
								InitModelMat4ByQuatf( &mLocalFrameBone, &handOutterKeyFrames[animationOutterKeyframeCount-1][dwBone-firstOutterBone].qRot, &handOutterKeyFrames[animationOutterKeyframeCount-1][dwBone-firstOutterBone].vPos );
								Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
								Mat4fMult(&handInvBind[dwBone],&mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
							}
						}
						else
						{
							f64 fCurrAnimationTime = fTotalTime * handStates[dwHand].m_fFrontTrigger;
							u32 dwCurrKeyFrame = 1;
							for( ; dwCurrKeyFrame < animationOutterKeyframeCount; ++dwCurrKeyFrame )
							{
								if( fCurrAnimationTime <= handOutterAnimTimeStamps[dwCurrKeyFrame])
								{
									break;
								}
							}
							u32 dwPrevKeyFrame = dwCurrKeyFrame-1;
							f32 fT = (f32)((fCurrAnimationTime - handOutterAnimTimeStamps[dwPrevKeyFrame]) / (handOutterAnimTimeStamps[dwCurrKeyFrame] - handOutterAnimTimeStamps[dwPrevKeyFrame]));
							Quatf rot;
							Vec3f pos;
							InitModelMat4ByQuatf( &mHandFrameBindBones[dwHand][0], &handSkeleton[0].qLocalRot, &handSkeleton[0].vLocalTrans ); 
							for( u32 dwBone = firstOutterBone; dwBone < (firstOutterBone+numOutterChannels); ++dwBone )
							{				
								Mat4f mLocalFrameBone;
								QuatfNormLerp(&handOutterKeyFrames[dwCurrKeyFrame][dwBone-firstOutterBone].qRot,&handOutterKeyFrames[dwPrevKeyFrame][dwBone-firstOutterBone].qRot,fT,&rot );
								Vec3fLerp(&handOutterKeyFrames[dwCurrKeyFrame][dwBone-firstOutterBone].vPos,&handOutterKeyFrames[dwPrevKeyFrame][dwBone-firstOutterBone].vPos,fT,&pos);
								InitModelMat4ByQuatf( &mLocalFrameBone, &rot, &pos );
								Mat4fMult(&mLocalFrameBone,&mHandFrameBindBones[dwHand][handBoneParents[dwBone]],&mHandFrameBindBones[dwHand][dwBone]);
								Mat4fMult(&handInvBind[dwBone], &mHandFrameBindBones[dwHand][dwBone],&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwBone]);
							}
						}

						dwStartingOffset = firstOutterBone;
						dwNumMats += numOutterChannels;
					}

					if( dwNumMats > 0 )
					{
						u8* pUploadBoneBufferData;
						if( FAILED( boneBuffer[oculusCurrentFrameIdx][dwHand]->Map( 0, nullptr, (void**) &pUploadBoneBufferData ) ) )
    					{
    					    return;
    					}
    					memcpy(pUploadBoneBufferData+(sizeof(Mat4f)*dwStartingOffset),&mHandFrameFinalBones[oculusCurrentFrameIdx][dwHand][dwStartingOffset],sizeof(Mat4f)*dwNumMats);
    					boneBuffer[oculusCurrentFrameIdx][dwHand]->Unmap( 0, nullptr );
    				}
				}
			}
		}


    	for( u32 dwEye = 0; dwEye < ovrEye_Count; ++dwEye )
    	{
    		//why would the following be different per eye?
    		Quatf eyeQuat;
    		eyeQuat.w = EyeRenderPose[dwEye].Orientation.w;
    		eyeQuat.x = EyeRenderPose[dwEye].Orientation.x;
    		eyeQuat.y = EyeRenderPose[dwEye].Orientation.y;
    		eyeQuat.z = EyeRenderPose[dwEye].Orientation.z;

    		Vec3f eyePos;
    		eyePos.x = EyeRenderPose[dwEye].Position.x; 
    		eyePos.y = EyeRenderPose[dwEye].Position.y;
    		eyePos.z = EyeRenderPose[dwEye].Position.z;

			Quatf eyeCamRot;
			QuatfMult( &eyeQuat, &qRot, &eyeCamRot );

			Vec3f vRotatedEyePos;
    		Vec3fRotByUnitQuat(&eyePos,&qRot,&vRotatedEyePos);
    		Vec3f eyeCamPos;
    		Vec3fAdd( &vRotatedEyePos, &startingPos, &eyeCamPos );

        	commandAllocators[(dwEye*oculusNUM_FRAMES) + oculusCurrentFrameIdx]->Reset();
			commandLists[dwEye]->Reset( commandAllocators[(dwEye*oculusNUM_FRAMES) + oculusCurrentFrameIdx], pipelineStateObject );

    		D3D12_RESOURCE_BARRIER presentToRenderBarrier;
    		presentToRenderBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    		presentToRenderBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    		presentToRenderBarrier.Transition.pResource = oculusEyeBackBuffers[(dwEye*oculusNUM_FRAMES) + oculusCurrentFrameIdx];
   			presentToRenderBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    		presentToRenderBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    		presentToRenderBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    		commandLists[dwEye]->ResourceBarrier( 1, &presentToRenderBarrier );
    		
    		//render here
    		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = eyeStartingRTVHandle[dwEye];
    		rtvHandle.ptr = (u64)rtvHandle.ptr + ( rtvDescriptorSize * oculusCurrentFrameIdx );
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = eyeDSVHandle[dwEye]; //need 2 textures cause they may be diff sizes
		
			commandLists[dwEye]->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
		
    		const float clearColor[] = { 0.5294f, 0.8078f, 0.9216f, 1.0f };
    		commandLists[dwEye]->ClearRenderTargetView( rtvHandle, clearColor, 0, NULL );
    		commandLists[dwEye]->ClearDepthStencilView( dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
    		
    		commandLists[dwEye]->SetGraphicsRootSignature( rootSignature ); //is this set with the pso?
			commandLists[dwEye]->SetGraphicsRoot32BitConstants( PIXEL_CB_ROOT_SLOT, 4 + 3, &pixelConstantBuffer ,0);

			commandLists[dwEye]->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ); 

    		commandLists[dwEye]->RSSetViewports( 1, &EyeViewports[dwEye] ); //does this always need to be set?
    		commandLists[dwEye]->RSSetScissorRects( 1, &EyeScissorRects[dwEye] ); //does this always need to be set?

			Mat4f mView;
    		InitViewMat4ByQuatf( &mView, &eyeCamRot, &eyeCamPos );
		
			Mat4f mProj;
			InitPerspectiveProjectionMat4fOculusDirectXRH( &mProj, oculusEyeRenderDesc[dwEye].Fov, 0.01f, 1000.0f ); //TODO tune near/far

			//ovrTimewarpProjectionDesc_FromProjection

    		Mat4f mVP;
    		Mat4fMult( &mView, &mProj, &mVP);
		
    		Mat4fMult(&mPlaneModel,&mVP, &vertexConstantBuffer.mvpMat);
    		InverseTransposeUpper3x3Mat4f( &mPlaneModel, &vertexConstantBuffer.nMat );
		
    		commandLists[dwEye]->SetGraphicsRoot32BitConstants( VERTEX_CB_ROOT_SLOT, ( 4 * 4 ) + ( ( ( 4 * 2 ) + 3 ) ), &vertexConstantBuffer ,0);
    		commandLists[dwEye]->IASetVertexBuffers( MAIN_VB_SLOT, 1, &planeVertexBufferView );
    		commandLists[dwEye]->IASetIndexBuffer( &planeIndexBufferView );
    		commandLists[dwEye]->DrawIndexedInstanced( planeIndexCount, 1, 0, 0, 0 );
		
		    Mat4fMult(&mCubeModel,&mVP, &vertexConstantBuffer.mvpMat);
    		InverseTransposeUpper3x3Mat4f( &mCubeModel, &vertexConstantBuffer.nMat );
		
    		commandLists[dwEye]->SetGraphicsRoot32BitConstants( VERTEX_CB_ROOT_SLOT, ( 4 * 4 ) + ( ( ( 4 * 2 ) + 3 ) ), &vertexConstantBuffer ,0);
    		commandLists[dwEye]->IASetVertexBuffers( MAIN_VB_SLOT, 1, &cubeVertexBufferView );
    		commandLists[dwEye]->IASetIndexBuffer( &cubeIndexBufferView );
    		commandLists[dwEye]->DrawIndexedInstanced( cubeIndexCount, 1, 0, 0, 0 );
		
    		commandLists[dwEye]->SetPipelineState( skinnedPipelineStateObject );
    		commandLists[dwEye]->SetGraphicsRootSignature( skinnedRootSignature );

    		for( u32 dwHand = 0; dwHand < ovrHand_Count; ++dwHand )
			{
				if( hwHandPresent[dwHand] )
				{
					Mat4fMult(&mHandModel[dwHand],&mVP, &vertexConstantBuffer.mvpMat);
    				InverseTransposeUpper3x3Mat4f( &mHandModel[dwHand], &vertexConstantBuffer.nMat );
				
    				commandLists[dwEye]->SetGraphicsRoot32BitConstants( VERTEX_CB_ROOT_SLOT, ( 4 * 4 ) + ( ( ( 4 * 2 ) + 3 ) ), &vertexConstantBuffer ,0);
//#if MAIN_DEBUG
//    				D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
//    				srvHandle.ptr = (u64)srvHandle.ptr + (srvDescriptorSize* ((dwHand * ovrHand_Count) + oculusCurrentFrameIdx));
//    				commandLists[dwEye]->SetGraphicsRootDescriptorTable(VERTEX_SB_ROOT_SLOT,srvHandle);
//#else
    				commandLists[dwEye]->SetGraphicsRootShaderResourceView( VERTEX_SB_ROOT_SLOT, boneBuffer[oculusCurrentFrameIdx][dwHand]->GetGPUVirtualAddress()); //i might be able to do this without a view but dangerously >:)
//#endif	
    				commandLists[dwEye]->IASetVertexBuffers( MAIN_VB_SLOT, 1, &handVertexBufferView );
    				commandLists[dwEye]->IASetIndexBuffer( &handIndexBufferView );
    				commandLists[dwEye]->DrawIndexedInstanced( handIndexCount, 1, 0, 0, 0 );
				}
			}

    		D3D12_RESOURCE_BARRIER renderToPresentBarrier;
    		renderToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    		renderToPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    		renderToPresentBarrier.Transition.pResource = oculusEyeBackBuffers[(dwEye*oculusNUM_FRAMES) + oculusCurrentFrameIdx];
   			renderToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    		renderToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    		renderToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    		commandLists[dwEye]->ResourceBarrier( 1, &renderToPresentBarrier );

    		if( FAILED( commandLists[dwEye]->Close() ) )
			{
				logError( "Command list failed to close, go through debug layer to see what command failed!\n" );
				CloseProgram();
				return;
			}
			
			ID3D12CommandList* ppCommandLists[] = { commandLists[dwEye] };
    		commandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

    		ovr_CommitTextureSwapChain( oculusSession, oculusEyeSwapChains[dwEye]); //does this muck with the command list/command queue?
    	}

    	//We specify the layer information now for the compositor, in the future use ovrLayerEyeFovDepth for asyn time warping
    	//They use the depth version of ovrLayerEyeFov_ instead to do positional timewarp, but for our example, do we even need positional timewarp?
    	ovrLayerEyeFov ld;
    	ld.Header.Type = ovrLayerType_EyeFov; //look into ovrLayerType
    	ld.Header.Flags = 0; //look into ovrLayerFlags
    	memset(ld.Header.Reserved,0,128);
    	ld.SensorSampleTime = fOculusFrameTiming;//fSensorSampleTime; //is this ok?
    	for (int dwEye = 0; dwEye < ovrEye_Count; ++dwEye)
    	{
    	    ld.ColorTexture[dwEye] = oculusEyeSwapChains[dwEye];
    	    ld.Viewport[dwEye] = oculusEyeRenderViewport[dwEye];
    	    ld.Fov[dwEye] = oculusHMDDesc.DefaultEyeFov[dwEye];
    	    ld.RenderPose[dwEye] = EyeRenderPose[dwEye];
    	}

    	ovrLayerHeader* oculusLayers = &ld.Header;
    	if( ovr_EndFrame( oculusSession, oculusFrameCount, nullptr, &oculusLayers, 1 ) < 0 )
    	{
#if MAIN_DEBUG
    		//TODO change to a retry create head set, maybe?
    		printf("end failed\n");
#endif
    		CloseProgram();
    	}
    	++oculusFrameCount;
    }
}


#if MAIN_DEBUG
s32 main()
#else
s32 APIENTRY WinMain(
    _In_ HINSTANCE Instance,
    _In_opt_ HINSTANCE PrevInstance,
    _In_ LPSTR CommandLine,
    _In_ s32 ShowCode )
#endif
{
	//TODO enter a searching for headset loop, once head set is found initialize it.
	//     then go into that rendering loop for that headset
	//     if that headset is disconnected for any reason go back to the searching for headset loop
	//     that way we can have special rendering loops for each type of hardware and even handle the user unplugging their headset
	//		have a little on screen window with one of those waiting spinner icons saying searching for headset
	//		also figure out minimal recreate. Do we need to reupload models and all that? Maybe for some headset but not others? or are they only on GPUs, so then no worry?
	//		pause the game too! 
	//TODO handle GPU device lost! If there is headset find GPU with headset attachted, (following is not our situation)If there is no headset Swap to next user preferred GPU or integrated graphics if they have none
	ovrInitParams oculusInitParams = { ovrInit_RequestVersion | ovrInit_FocusAware, OVR_MINOR_VERSION, NULL, 0, 0 };
	if( ovr_Initialize( &oculusInitParams ) >= 0 ) //can this persist outside of loop when trying to recreate headset? or does this need to be in retry create loop?
	{
		InitStartingCamera();
		if( InitOculusHeadset() != 0 )
		{
			ovr_Shutdown(); //how to handle this on retry create on headset unplugged 
			return -1;
		}

		LARGE_INTEGER PerfCountFrequencyResult;
    	QueryPerformanceFrequency( &PerfCountFrequencyResult );
    	int64_t PerfCountFrequency = PerfCountFrequencyResult.QuadPart;
    	LARGE_INTEGER LastCounter;
    	QueryPerformanceCounter( &LastCounter );

		InitStartingGameState();
		InitHeadsetGraphicsState();
		if( InitDirectX12() )
		{
			ovr_Destroy( oculusSession );
			ovr_Shutdown();
			return -1;
		}
		InitStartingSkeletons( oculusNUM_FRAMES );

		while( Running )
		{
    		u64 EndCycleCount = __rdtsc();
    	
    		LARGE_INTEGER EndCounter;
    		QueryPerformanceCounter(&EndCounter);
    	
    		//Display the value here
    		s64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
    		f32 deltaTime = CounterElapsed / (f32)PerfCountFrequency ;
    		f64 MSPerFrame = (f64) ( ( 1000.0f * CounterElapsed ) / (f64)PerfCountFrequency );
    		f64 FPS = PerfCountFrequency / (f64)CounterElapsed;
    		LastCounter = EndCounter;

#if MAIN_DEBUG
    		//char buf[64];
    		//sprintf_s( &buf[0], 64, "%s: fps %f", WindowName, FPS );
    		//SetWindowTextA( MainWindowHandle, &buf[0] );
#endif


			MSG Message;
        	while( PeekMessage( &Message, 0, 0, 0, PM_REMOVE ) )
        	{
        		switch( Message.message )
        		{
        	        case WM_QUIT:
        	        {
        	        	CloseProgram();
        	        	break;
        	        }
        	        case WM_SYSKEYDOWN:
        	        case WM_SYSKEYUP:
        	        case WM_KEYDOWN:
        	        case WM_KEYUP:
        	        {
        	           uint32_t VKCode = (uint32_t) Message.wParam;
        	           bool WasDown = ( Message.lParam & ( 1 << 30 ) ) != 0;
        	           bool IsDown = ( Message.lParam & ( 1 << 31 ) ) == 0;
        	           bool AltKeyWasDown = ( Message.lParam & ( 1 << 29 ) );
        	           switch( VKCode )
        	           {
        	                case VK_ESCAPE:
        	                {
        	                	if( WasDown != 1 && WasDown != IsDown )
        	                	{
        	                		TogglePause();
        	                	}
        	                	break;
        	                }
        	                case VK_F4:
        	                {
        	                	if( AltKeyWasDown )
        	                	{
        	                		CloseProgram();
        	                	}
        	                	break;
        	                }
        	                default:
        	                {
        	                	break;
        	                }
        	            }
        	            break;
        	        }
        	        default:
        	        {
        	        	TranslateMessage( &Message );
        	        	DispatchMessage( &Message );
        	        	break;
        	        }
            	}
        	}

        	//TODO MOVE OCULUS SESSION STATUS OUTSIDE DRAW SCENE AND MOVE IF STATEMENTS OUTSIDE OF IT
        	//DEAL WITH OCULUS CONTEXT LOST LIKE DEMO

        	//todo maybe add a #define for multiplayer where updates still happen but rendering does not on minimization
        	DrawScene( ( 1 - isPaused ) * deltaTime );
		}
		//free(commandAllocators);
		ovr_Destroy( oculusSession );
		ovr_Shutdown();
	}

	return 0;
}