struct PixelInput
{
	float4 pos : SV_Position;
	float3 worldNormal : NORMAL;
	float4 color : COLOR;	
};

cbuffer uniformsCB : register(b1) //can this be b0 even though there is a b0 in the vertex shader?
{
    float4 vLightColor;
	float3 vInvLightDir;
};

float4 main( PixelInput inPixel ) : SV_Target
{
	                                                //diffuse                                         //ambient
	return saturate( inPixel.color * vLightColor *( max(dot(normalize(inPixel.worldNormal),vInvLightDir),0.0f) + float4(0.45,0.45,0.45,1.0f) ) );
	//return float4( (normalize(inPixel.worldNormal)*0.5f)+0.5f, 1.0f );
}