//https://www.gamedev.net/forums/topic/624529-structured-buffers-vs-constant-buffers/

//is row or column major order more efficent in shader code?

#define STRUCTURED_BUFFER 1
#define ARRAY_IN_STRUCTURED_BUFFER 1

#if __SHADER_TARGET_MAJOR >= 5
#if __SHADER_TARGET_MAJOR > 5 ||  ( __SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1 )
 //5.1+
#if ARRAY_IN_STRUCTURED_BUFFER && STRUCTURED_BUFFER
struct Bones
{
    float4x4 boneMat[MAX_BONES];
};
#else
struct Bones
{
    float4x4 boneMat;
};
#endif
#if STRUCTURED_BUFFER
StructuredBuffer <Bones> bonesSB : register(t0); //register(t0, space0);
#else
ConstantBuffer <Bones> bonesSB[MAX_BONES] : register(b0); //register(b0, space0);
#endif
#else
 //5.0
#if STRUCTURED_BUFFER
#if ARRAY_IN_STRUCTURED_BUFFER && STRUCTURED_BUFFER
struct Bones
{
    float4x4 boneMat[MAX_BONES];
};
#else
struct Bones
{
    float4x4 boneMat;
};
#endif
StructuredBuffer <Bones> bonesSB : register(t0); //register(t0, space0);
#else
cbuffer bonesCB[MAX_BONES] : register(b0)
{
	float4x4 boneMat;
};
#endif
#endif
#elif __SHADER_TARGET_MAJOR == 4
 //4.0
cbuffer bonesCB[MAX_BONES] : register(b0)
{
	float4x4 boneMat;
};
#else
#endif

struct VertexInput
{
	float3 pos : POS;
	float3 localNormal : NORMAL;
	uint4 skinJoints : JOINT; //need to switch to 8/16 bit bone index
	float4 skinWeights : WEIGHT;
	float4 color : COLOR; //todo we can save a byte on opaque objects by assuming alpha = 1!
};

struct VertexOutput
{
	float4 pos : SV_Position;
	float3 worldNormal : NORMAL;
	float4 color : COLOR;
};

////https://www.gamedev.net/forums/topic/624529-structured-buffers-vs-constant-buffers/
//https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-per-component-math
//https://www.gamedev.net/forums/topic/650415-d3dcompile-and-pragma-pack_matrix/5111786/

//for all
//#pragma pack_matrix( row_major )
//#pragma pack_matrix( column_major )
//for a singular
//row_major float4x4
//column_major float4x4

//vs_5_0 way
cbuffer uniformsCB : register(b0)
{
//matrix <float, 4, 4>
    float4x4 mvpMat; //TODO split it up maybe...
	float3x3 nMat;
};

/* //vs_5_1 way
struct Uniforms
{
	float4x4 mvpMat;
	float3x3 nMat;
};

ConstantBuffer<Uniforms> uniformsCB : register(b0, space0);
//There is also StructuredBuffer too!
*/

//TODO verify which is of the 4 is fastest for a lot of vertices
/*

float3 pos -> mul( float4( inVert.pos, 1.0f), mvpMat ); or mul( mvpMat, float4( inVert.pos, 1.0f) );
or
float4 pos -> mul( inVert.pos, mvpMat ); or mul( mvpMat, inVert.pos );
*/

VertexOutput main( VertexInput inVert )
{
	VertexOutput outVert;
	//vs_5_0 way

#if ARRAY_IN_STRUCTURED_BUFFER && STRUCTURED_BUFFER
 	float4 pos = mul( bonesSB[0].boneMat[inVert.skinJoints.x], float4( inVert.pos, 1.0f) ) * inVert.skinWeights.x;
 	pos += mul( bonesSB[0].boneMat[inVert.skinJoints.y], float4( inVert.pos, 1.0f) ) * inVert.skinWeights.y;
 	pos += mul( bonesSB[0].boneMat[inVert.skinJoints.z], float4( inVert.pos, 1.0f) ) * inVert.skinWeights.z;
 	pos += mul( bonesSB[0].boneMat[inVert.skinJoints.w], float4( inVert.pos, 1.0f) ) * inVert.skinWeights.w;
#else
 	float4 pos = mul( bonesSB[inVert.skinJoints.x].boneMat, float4( inVert.pos, 1.0f) ) * inVert.skinWeights.x;
 	pos += mul( bonesSB[inVert.skinJoints.y].boneMat, float4( inVert.pos, 1.0f) ) * inVert.skinWeights.y;
 	pos += mul( bonesSB[inVert.skinJoints.z].boneMat, float4( inVert.pos, 1.0f) ) * inVert.skinWeights.z;
 	pos += mul( bonesSB[inVert.skinJoints.w].boneMat, float4( inVert.pos, 1.0f) ) * inVert.skinWeights.w;
#endif

	outVert.pos = mul( mvpMat, pos );
	outVert.worldNormal = mul( nMat, inVert.localNormal );
	outVert.color = inVert.color;
	return outVert;
}