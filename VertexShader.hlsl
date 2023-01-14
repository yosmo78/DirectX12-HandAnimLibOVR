struct VertexInput
{
	float3 pos : POS;
	float3 localNormal : NORMAL;
	float4 color : COLOR; //todo we can save a byte on opaque objects by assuming alpha = 1!
};

struct VertexOutput
{
	float4 pos : SV_Position;
	float3 worldNormal : NORMAL;
	float4 color : COLOR;
};

//vs_5_0 way
cbuffer uniformsCB : register(b0)
{
    float4x4 mvpMat;
	float3x3 nMat;
};

/* //vs_5_1 way
struct Uniforms
{
	float4x4 mvpMat;
	float3x3 nMat;
};

ConstantBuffer<Uniforms> uniformsCB : register(b0, space0);
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
	outVert.pos = mul( mvpMat, float4( inVert.pos, 1.0f) );
	outVert.worldNormal = mul( nMat, inVert.localNormal );
	//vs_5_1 way
	//outVert.pos = mul( uniformsCB.mvpMat, float4( inVert.pos, 1.0f) );
	//outVert.worldNormal = mul( uniformsCB.nMat, inVert.localNormal );
	outVert.color = inVert.color;
	return outVert;
}