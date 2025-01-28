#pragma once
#include<Windows.h>
#include<d3d12.h>
#include<dxgi1_6.h>
#include<DirectXTex.h>
#include<vector>
#include<DirectXMath.h>
#include<wrl.h>
#include<unordered_map>
#include<memory>
#include<array>
#include<string>

class PMDActor;
class PMDRenderer;
using Microsoft::WRL::ComPtr;
///DirectX12の各要素とか関数を
///ラップしてるだけのクラス
class Dx12Wrapper
{
private:
	struct MultiTexture{
		ComPtr<ID3D12Resource> texBuff;//通常テクスチャバッファ
		ComPtr<ID3D12Resource> sphBuff;//SPHテクスチャ
		ComPtr<ID3D12Resource> spaBuff;//SPAテクスチャ
		ComPtr<ID3D12Resource> toonBuff;//トゥーンテクスチャ
	};

	HWND _hwnd;

	//基本的な奴(DXGI)
	ComPtr < IDXGIFactory4> _dxgiFactory;
	ComPtr < IDXGISwapChain4> _swapchain;

	//基本的な奴(デバイス)
	ComPtr < ID3D12Device> _dev;


	//コマンドキュー(コマンド実行の単位)
	ComPtr < ID3D12CommandQueue> _cmdQue;

	//深度バッファ用バッファ
	ComPtr<ID3D12Resource> _depthBuffer;
	//深度バッファビュー用スクリプタヒープ
	ComPtr<ID3D12DescriptorHeap> _dsvHeap;
	//シャドウマップ用深度バッファ
	ComPtr<ID3D12Resource> _lightDepthBuffer;
	

	bool CreateDepthBuffer();
	bool CreateDSV();

	//レンダーターゲットビュー用デスクリプタヒープ
	ComPtr<ID3D12DescriptorHeap> _rtvDescHeap;
	//スワップチェインが持っているリソースへのポインタ
	std::vector<ID3D12Resource*> _backBuffers;

	//コマンドリストを格納するためのメモリ領域
	ComPtr <ID3D12CommandAllocator> _cmdAlloc = nullptr;
	//コマンドリスト本体(コマンドアロケータに命令を登録するための
	//インターフェイス)
	ComPtr<ID3D12GraphicsCommandList> _cmdList = nullptr;
	
	//待ちのためのフェンス
	ComPtr<ID3D12Fence> _fence;
	UINT64 _fenceValue;

	//無駄読みしないようにテクスチャのテーブルを作っておく
	std::unordered_map<std::wstring, ComPtr<ID3D12Resource>> _textureTable;

	bool CreateTextureFromImageData(const DirectX::Image* img, ComPtr<ID3D12Resource>& buff,bool isDiscrete=false);

	ComPtr<ID3D12Resource> _whiteTex;//白テクスチャ
	///乗算用の真っ白テクスチャ(乗算されても影響が出ない)を作ります。
	bool CreateWhiteTexture();

	ComPtr<ID3D12Resource> _blackTex;//黒テクスチャ
	///加算用の真っ黒テクスチャ(加算されても影響が出ない)を作ります。
	bool CreateBlackTexture();

	ComPtr<ID3D12Resource> _gradTex;//グレーグラデーションテクスチャ
	//トゥーンがない場合の階調を定義する
	bool CreateGradationTexture();

	ComPtr < ID3D12Resource> _sceneCB;//座標変換定数バッファ
	ComPtr < ID3D12DescriptorHeap> _sceneHeap;//座標変換CBVヒープ
	///座標変換用定数バッファおよび定数バッファビューを作成する
	bool CreateTransformConstantBuffer();
	bool CreateTransformBufferView();

	struct SceneMatrix {
		DirectX::XMMATRIX view;//ビュー
		DirectX::XMMATRIX proj;//プロジェクション
		DirectX::XMMATRIX invproj;//プロジェクション
		DirectX::XMMATRIX lightCamera;//ライトから見たビュー
		DirectX::XMMATRIX shadow;//影行列
		DirectX::XMFLOAT4 lightVec;//ライトベクトル
		DirectX::XMFLOAT3 eye;//視点
		bool isSelfShadow;//シャドウマップフラグ
	};


	SceneMatrix* _mappedScene;
	float _bgColor[4];
	//視点(カメラの位置)XMVECTOR
	//注視点(見る対象の位置)XMVECTOR
	//上ベクトル(上)XMVECTOR
	DirectX::XMFLOAT3 _eye;
	DirectX::XMFLOAT3 _target;
	DirectX::XMFLOAT3 _up;
	float _fov = DirectX::XM_PI/6;
	DirectX::XMFLOAT3 _lightVec;
	bool _isSelfShadow;

	bool CreateCommandList();
	void Barrier(ID3D12Resource* p,
		D3D12_RESOURCE_STATES before, 
		D3D12_RESOURCE_STATES after);

	//std::vector<PMDActor*> _actors;

	//1枚目レンダリング用
	//いわゆるペラポリに張り付けるための絵の
	//メモリリソースとそのビュー
	ComPtr<ID3D12DescriptorHeap> _peraRTVHeap;
	ComPtr<ID3D12DescriptorHeap> _peraSRVHeap;
	std::array<ComPtr<ID3D12Resource>,2> _pera1Resources;
	//１枚目ペラポリのためのリソースとビューを
	//作成
	bool CreatePera1ResourceAndView();
	
	//ペラポリ用頂点バッファ(N字の4点)
	ComPtr<ID3D12Resource> _peraVB;
	D3D12_VERTEX_BUFFER_VIEW _peraVBV;

	//ペラポリ用パイプライン＆ルートシグネチャ
	ComPtr<ID3D12PipelineState> _peraPipeline;
	ComPtr<ID3D12RootSignature> _peraRS;

	//ペラポリに投げる定数バッファ
	ComPtr<ID3D12Resource> _peraCB;
	ComPtr<ID3D12DescriptorHeap> _peraCBVHeap;
	bool CreateConstantBufferForPera();

	struct PostSetting {
		bool isDebugDisp;//デバッグ表示
		bool isSSAO;//SSAOオン
		DirectX::XMFLOAT4 bloomColor;//ブルームカラー
	};
	ComPtr<ID3D12Resource> _postSettingResource;
	PostSetting* _mappedPostSetting;
	ComPtr<ID3D12DescriptorHeap> _postSettingDH;
	bool CreatePostSetting();


	//歪み用ノーマルマップ
	ComPtr<ID3D12Resource> _distBuff;
	ComPtr<ID3D12DescriptorHeap> _distSRVHeap;
	//深度値テクスチャ用
	ComPtr<ID3D12DescriptorHeap> _depthSRVHeap;
	bool CreateDistortion();
	bool CreateDepthSRV();

	//プリミティブ用頂点バッファ
	std::vector<ComPtr<ID3D12Resource>> _primitivesVB;
	std::vector<D3D12_VERTEX_BUFFER_VIEW> _primitivesVBV;

	//プリミティブ用インデックスバッファ
	std::vector<ComPtr<ID3D12Resource>> _primitivesIB;
	std::vector<D3D12_INDEX_BUFFER_VIEW> _primitivesIBV;
	bool CreatePrimitives();
	
	ComPtr<ID3D12RootSignature> _primitveRS;
	ComPtr<ID3D12PipelineState> _primitivePipeline;
	bool CreatePrimitivePipeline();
	bool CreatePrimitiveRootSignature();
	
	ComPtr<ID3D12PipelineState> _blurPipeline;//画面全体ぼかし用パイプライン
	std::array<ComPtr<ID3D12Resource>, 2> _bloomBuffers;//ブルーム用バッファ
	ComPtr<ID3D12Resource> _dofBuffer;//被写界深度用ぼかしバッファ
	bool CreateBloomBuffer();
	bool CreateBlurForDOFBuffer();


	ComPtr<ID3D12Resource> _aoBuffer;
	ComPtr<ID3D12PipelineState> _aoPipeline;
	ComPtr<ID3D12DescriptorHeap> _aoRTVDH;
	ComPtr<ID3D12DescriptorHeap> _aoSRVDH;

	bool CreateAmbientOcclusionBuffer();
	bool CreateAmbientOcclusionDescriptorHeap();

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeapForImgui();
	ComPtr<ID3D12DescriptorHeap> _heapForImgui;

public:
	Dx12Wrapper(HWND hwnd);
	~Dx12Wrapper();

	ID3D12Device* Device() {
		return _dev.Get();
	}
	ID3D12GraphicsCommandList* CmdList() {
		return _cmdList.Get();
	}
	ID3D12CommandQueue* CmdQue() {
		return _cmdQue.Get();
	}


	bool Init();
	bool CreateRenderTargetView();

	ComPtr<ID3D12Resource> WhiteTexture();
	ComPtr<ID3D12Resource> BlackTexture();
	ComPtr<ID3D12Resource> GradTexture();



	bool CreatePeraVertex();
	bool CreatePeraPipeline();

	//ライトからの描画(影用)の準備
	bool PreDrawShadow();

	//ペラポリゴンへの描画準備
	bool PreDrawToPera1();

	//ペラポリゴンへの描画
	///プリミティブ形状(平面、円柱、円錐、球)を描画
	void DrawPrimitiveShapes();
	void DrawToPera1(std::shared_ptr<PMDRenderer> renderer);
	void DrawToPera2();
	void DrawShrinkTextureForBlur();
	void DrawAmbientOcculusion();
	//画面のクリア
	bool Clear();

	//描画
	void Draw(std::shared_ptr<PMDRenderer> renderer);

	void SetCameraSetting();

	//フリップ
	void Flip();
	void WaitForCommandQueue();

	bool LoadPictureFromFile(std::wstring filepath, ComPtr<ID3D12Resource>& buff);

	void SetFov(float angle);
	void SetEyePosition(float x, float y, float z);
	void MoveEyePosition(float x, float y, float z);

	DirectX::XMVECTOR GetCameraPosition();

	ComPtr<ID3D12DescriptorHeap> GetHeapForImgui();
	void SetDebugDisplay(bool flg);///デバッグ表示のON / OFF
	void SetSSAO(bool flg);///アンビエントオクルージョンのON / OFF
	void SetSelfShadow(bool flg);///セルフシャドウON / OFF
	void SetLightVector(float vec[3]);///光線ベクトル(xyzベクトル)
	void SetBackColor(float col[4]);///背景色の変更
	void SetBloomColor(float col[3]);///ブルームの色付け

};

