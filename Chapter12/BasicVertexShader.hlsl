#include"Type.hlsli"


//シーン管理用スロット
cbuffer SceneBuffer : register(b1) {
	matrix view;//ビュー
	matrix proj;//プロジェクション
	float3 eye;//視点
};

//アクター座標変換用スロット
cbuffer TransBuffer : register(b2) {
	matrix world;
}

//ボーン行列配列
cbuffer BonesBuffer : register(b3) {
	matrix bones[512];
}



//頂点シェーダ(頂点情報から必要なものを次の人へ渡す)
//パイプラインに投げるためにはSV_POSITIONが必要
BasicType BasicVS(float4 pos:POSITION,float4 normal:NORMAL,float2 uv:TEXCOORD,min16uint2 boneno:BONENO,min16uint weight:WEIGHT) {
	//1280,720を直で使って構わない。
	BasicType output;
	float fWeight = float(weight) / 100.0f;
	matrix conBone = bones[boneno.x]*fWeight + 
						bones[boneno.y]*(1.0f - fWeight);

	output.pos = mul(world, 
						mul(conBone,pos)
					);
	output.svpos = mul(proj,mul(view, output.pos));
	output.uv = uv;
	normal.w = 0;
	output.normal = mul(world,normal);
	output.weight = (float)weight/100.0f;
	output.boneno = boneno[0]/122.0;
	//output.uv = uv;
	return output;
}
