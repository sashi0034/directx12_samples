#include "Dx12Wrapper.h"
#include"Application.h"
#include<DirectXMath.h>
#include<d3dcompiler.h>
#include<cassert>
#include<memory>
#include"PMDActor.h"
#include"PMDRenderer.h"
#include<d3dx12.h>
#include"Helper.h"
#include"PrimitiveActor.h"


#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"DirectXTex.lib")


using namespace DirectX;
using namespace std;

namespace {
	enum class DepthType {//深度タイプ
		defaultDepth=0,//普通描画に使用する深度
		lightDepth=1,//シャドウマップに使用するライトからの深度
	};
}

constexpr uint32_t shadow_difinition = 1024;

bool
Dx12Wrapper::CreateConstantBufferForPera() {
	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	auto gWeights = GetGaussianValues(5.0f, 8);

	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(
		AligmentedValue(gWeights.size()*sizeof(gWeights[0]), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
	);

	auto result = _dev->CreateCommittedResource(&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(_peraCB.ReleaseAndGetAddressOf()));

	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptorHeapDesc.NodeMask = 0;
	descriptorHeapDesc.NumDescriptors = 1;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	result = _dev->CreateDescriptorHeap(&descriptorHeapDesc,
		IID_PPV_ARGS(&_peraCBVHeap));

	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = _peraCB->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = resDesc.Width;
	_dev->CreateConstantBufferView(&cbvDesc, _peraCBVHeap->GetCPUDescriptorHandleForHeapStart());

	float* mappedWeights = nullptr;
	result = _peraCB->Map(0, nullptr, (void**)&mappedWeights);
	copy(gWeights.begin(), gWeights.end(), mappedWeights);
	_peraCB->Unmap(0, nullptr);
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	return true;

}

Dx12Wrapper::Dx12Wrapper(HWND hwnd): _hwnd(hwnd),
	_eye(0,15,-25),
	_target(0,10,0),
	_up(0,1,0)
{
	

}


Dx12Wrapper::~Dx12Wrapper()
{
}

bool
Dx12Wrapper::CreateDepthSRV() {
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	auto result = _dev->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&_depthSRVHeap));
	if (!CheckResult(result)) {
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC resDesc = {};
	resDesc.Format = DXGI_FORMAT_R32_FLOAT;
	resDesc.Texture2D.MipLevels = 1;
	resDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	resDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	auto handle=_depthSRVHeap->GetCPUDescriptorHandleForHeapStart();
	//通常デプス→テクスチャ用
	_dev->CreateShaderResourceView(_depthBuffer.Get(),
		&resDesc,
		handle);
	//ライトデプス→テクスチャ用
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_dev->CreateShaderResourceView(_lightDepthBuffer.Get(),
		&resDesc,
		handle);

	return true;
}

bool 
Dx12Wrapper::Init() {
	ID3D12Debug* debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	debug->EnableDebugLayer();
	debug->Release();

	HRESULT result=S_OK;
	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};
	for (auto level : levels) {
		if (SUCCEEDED(
			D3D12CreateDevice(
				nullptr,
				level,
				IID_PPV_ARGS(&_dev)
			))) {
			break;
		}
	}
	if (_dev == nullptr) {
		return false;
	}

	if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,IID_PPV_ARGS(_dxgiFactory.ReleaseAndGetAddressOf())))) {
		return false;
	}

	//コマンドキューの作成
	D3D12_COMMAND_QUEUE_DESC cmdQDesc = {};
	cmdQDesc.NodeMask = 0;//アダプターマスクなし
	cmdQDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	cmdQDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	if (FAILED(_dev->CreateCommandQueue(&cmdQDesc, IID_PPV_ARGS(_cmdQue.ReleaseAndGetAddressOf())))) {
		return false;
	}

	Application& app = Application::Instance();
	auto wsize = app.GetWindowSize();
	DXGI_SWAP_CHAIN_DESC1 swDesc = {};
	swDesc.BufferCount = 2;//フリップに使用する紙は２つ
	swDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swDesc.Flags = 0;
	swDesc.Width = wsize.width;
	swDesc.Height = wsize.height;
	swDesc.SampleDesc.Count = 1;
	swDesc.SampleDesc.Quality = 0;
	swDesc.Scaling = DXGI_SCALING_STRETCH;
	swDesc.Stereo = false;
	swDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	result = _dxgiFactory->CreateSwapChainForHwnd(
		_cmdQue.Get(),
		_hwnd,
		&swDesc,
		nullptr,
		nullptr,
		(IDXGISwapChain1**)(_swapchain.ReleaseAndGetAddressOf())
	);
	if (FAILED(result)) {
		return false;
	}

	if (!CreateRenderTargetView()) {
		return false;
	}

	//深度バッファ作成
	if (!CreateDepthBuffer()) {
		return false;
	}
	//深度バッファビュー作成
	if (!CreateDSV()) {
		return false;
	}
	//深度バッファを画像として使うビューを作成
	if (!CreateDepthSRV()) {
		return false;
	}

	//コマンドリストの作成
	if (!CreateCommandList()) {
		return false;
	}

	//フェンスの作成
	_fenceValue = 0;//比較用
	result = _dev->CreateFence(
		_fenceValue, //初期値
		D3D12_FENCE_FLAG_NONE, //特にフラグなし
		IID_PPV_ARGS(_fence.ReleaseAndGetAddressOf()));//_fenceを作る
	assert(SUCCEEDED(result));


	if (!CreateTransformConstantBuffer()) {
		return false;
	}

	if (!CreateWhiteTexture()) {
		return false;
	}
	if (!CreateBlackTexture()) {
		return false;
	}
	if (!CreateGradationTexture()) {
		return false;
	}
	if (!CreateTransformBufferView()) {
		return false;
	}

	if (!CreatePrimitives()) {
		return false;
	}
	if (!CreatePrimitiveRootSignature()) {
		return false;
	}
	if (!CreatePrimitivePipeline()) {
		return false;
	}

	if (!CreateDistortion()) {
		return false;
	}

	//ペラ２枚目用
	if (!CreatePera2Resource()) {
		return false;
	}

	//ペラポリ用
	if (!CreatePera1ResourceAndView()) {
		return false;
	}
	if (!CreateConstantBufferForPera()) {
		return false;
	}
	if (!CreatePeraVertex()) {
		return false;
	}
	if (!CreatePeraPipeline()) {
		return false;
	}

	return true;

}
bool 
Dx12Wrapper::CreatePeraVertex() {
	struct PeraVertex {
		XMFLOAT3 pos;
		XMFLOAT2 uv;
	};
	PeraVertex pv[4] = { {{-1,-1,0.1},{0,1}},
						{{-1,1,0.1},{0,0}},
						{{1,-1,0.1},{1,1}},
						{{1,1,0.1},{1,0}} };

	auto result = _dev->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(pv)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(_peraVB.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	//マップ忘れてました。
	PeraVertex* mappedPera = nullptr;
	_peraVB->Map(0, nullptr, (void**)&mappedPera);
	copy(begin(pv), end(pv), mappedPera);
	_peraVB->Unmap(0, nullptr);

	_peraVBV.BufferLocation = _peraVB->GetGPUVirtualAddress();
	_peraVBV.SizeInBytes = sizeof(pv);
	_peraVBV.StrideInBytes = sizeof(PeraVertex);
	return true;
}

ComPtr<ID3D12Resource> 
Dx12Wrapper::WhiteTexture() {
	return _whiteTex;
}
ComPtr<ID3D12Resource> 
Dx12Wrapper::BlackTexture() {
	return _blackTex;
}
ComPtr<ID3D12Resource> 
Dx12Wrapper::GradTexture() {
	return _gradTex;
}

bool 
Dx12Wrapper::CreateRenderTargetView() {
	DXGI_SWAP_CHAIN_DESC1 swDesc = {};
	auto result = _swapchain->GetDesc1(&swDesc);

	//レンダーターゲットビュー用のヒープを作成
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	desc.NodeMask = 0;
	desc.NumDescriptors = swDesc.BufferCount;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	result = _dev->CreateDescriptorHeap(&desc,
						IID_PPV_ARGS(&_rtvDescHeap));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}
	_backBuffers.resize(swDesc.BufferCount);

	auto handle=_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
	auto incSize=_dev->GetDescriptorHandleIncrementSize(
							D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	//スワップチェインから、フリップ対象のリソースを取得
	for (int i = 0; i < swDesc.BufferCount; ++i) {
		//GetBufferはスワップチェインが持ってるi番目の
		//リソースを第二引数に入れてくれる
		result = _swapchain->GetBuffer(i, IID_PPV_ARGS(&_backBuffers[i]));
		assert(SUCCEEDED(result));
		if (FAILED(result)) {
			return false;
		}
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		_dev->CreateRenderTargetView(_backBuffers[i], &rtvDesc, handle);
		handle.ptr += incSize;
	}

	return true;

}

bool 
Dx12Wrapper::CreateCommandList() {
	auto result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
										IID_PPV_ARGS(_cmdAlloc.ReleaseAndGetAddressOf()));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}
	result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		_cmdAlloc.Get(), nullptr, IID_PPV_ARGS(_cmdList.ReleaseAndGetAddressOf()));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}
	return true;
}

bool 
Dx12Wrapper::PreDrawShadow() {
	auto handle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	
	_cmdList->OMSetRenderTargets(0, nullptr, false, &handle);
	
	_cmdList->ClearDepthStencilView(handle,
		D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	auto wsize = Application::Instance().GetWindowSize();

	ID3D12DescriptorHeap* heaps[] = { _sceneHeap.Get() };

	heaps[0] = _sceneHeap.Get();
	_cmdList->SetDescriptorHeaps(1, heaps);
	auto sceneHandle = _sceneHeap->GetGPUDescriptorHandleForHeapStart();
	_cmdList->SetGraphicsRootDescriptorTable(1, sceneHandle);

	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT(0.0f, 0.0f, shadow_difinition, shadow_difinition);
	_cmdList->RSSetViewports(1, &vp);//ビューポート

	CD3DX12_RECT rc(0, 0, shadow_difinition, shadow_difinition);
	_cmdList->RSSetScissorRects(1, &rc);//シザー(切り抜き)矩形

	return true;
}

bool 
Dx12Wrapper::PreDrawToPera1() {
	Barrier(_peraResource.Get()	,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	auto rtvHeapPointer = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart();
	_cmdList->OMSetRenderTargets(1, &rtvHeapPointer, false, &_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	//クリアカラー		 R   G   B   A
	float clsClr[4] = { 0.5,0.5,0.5,1.0 };
	_cmdList->ClearRenderTargetView(rtvHeapPointer, clsClr, 0, nullptr);
	_cmdList->ClearDepthStencilView(_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
		D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	_cmdList->SetDescriptorHeaps(1, _sceneHeap.GetAddressOf());
	auto sceneHandle = _sceneHeap->GetGPUDescriptorHandleForHeapStart();
	_cmdList->SetGraphicsRootDescriptorTable(1, sceneHandle);


	return true;
}

bool
Dx12Wrapper::Clear() {
	Barrier(_peraResource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);


	//バックバッファのインデックスを取得する
	auto bbIdx=_swapchain->GetCurrentBackBufferIndex();

	Barrier(_backBuffers[bbIdx],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	auto rtvHeapPointer=_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHeapPointer.ptr+=bbIdx*_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	
	_cmdList->OMSetRenderTargets(1, &rtvHeapPointer, false, nullptr);
	
	//クリアカラー		 R   G   B   A
	float clsClr[4] = { 0.2,0.5,0.5,1.0 };
	_cmdList->ClearRenderTargetView(rtvHeapPointer, clsClr, 0, nullptr);
	//_cmdList->ClearDepthStencilView(_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
	//	D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	return true;
}

void Dx12Wrapper::Barrier(ID3D12Resource* p,
	D3D12_RESOURCE_STATES before,
	D3D12_RESOURCE_STATES after){
	_cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(p, before, after, 0));
}

void 
Dx12Wrapper::Flip() {
	auto bbIdx = _swapchain->GetCurrentBackBufferIndex();

	Barrier(_backBuffers[bbIdx],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);

	_cmdList->Close();
	ID3D12CommandList* cmds[] = { _cmdList.Get() };
	_cmdQue->ExecuteCommandLists(1, cmds);
	
	WaitForCommandQueue();

	_cmdAlloc->Reset();
	_cmdList->Reset(_cmdAlloc.Get(), nullptr);

	//Present関数が、DxLibにおけるScreenFlipみたいなもんです。
	//Presentの第一引数が「何回垂直同期を待つか」です。
	//垂直同期信号ってのはブラウン管の時代に走査線が一周する
	//周期1/60だったんだけど、今はそんなのないので待たない
	auto result = _swapchain->Present(0, 0);
	assert(SUCCEEDED(result));
}

void Dx12Wrapper::WaitForCommandQueue()
{
	//仮に初回として_fenceValueが0だったとします
	_cmdQue->Signal(_fence.Get(), ++_fenceValue);
	//↑の命令直後では_fenceValueは1で、
	//GetCompletedValueはまだ0です。
	if (_fence->GetCompletedValue() < _fenceValue) {
		//もしまだ終わってないなら、イベント待ちを行う
		//↓そのためのイベント？あとそのための_fenceValue
		auto event = CreateEvent(nullptr, false, false, nullptr);
		//フェンスに対して、CompletedValueが_fenceValueに
		//なったら指定のイベントを発生させるという命令↓
		_fence->SetEventOnCompletion(_fenceValue, event);
		//↑まだイベント発生しない
		//↓イベントが発生するまで待つ
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
}

void 
Dx12Wrapper::SetFov(float angle) {
	_fov = angle;
}
void 
Dx12Wrapper::SetEyePosition(float x, float y, float z) {
	_eye.x = x;
	_eye.y = y;
	_eye.z = z;
}
void 
Dx12Wrapper::MoveEyePosition(float x, float y, float z) {
	_eye.x += x;
	_eye.y += y;
	_eye.z += z;
	_target.x += x;
	_target.y += y;
	_target.z += z;

}

bool 
Dx12Wrapper::CreateDepthBuffer() {
	D3D12_HEAP_PROPERTIES heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	auto wsize = Application::Instance().GetWindowSize();
	D3D12_RESOURCE_DESC resdesc = 
		CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, wsize.width, wsize.height);
	resdesc.Flags= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE cv = {};
	cv.DepthStencil.Depth = 1.0f;
	cv.Format = DXGI_FORMAT_D32_FLOAT;

	auto result = _dev->CreateCommittedResource(&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&cv,
		IID_PPV_ARGS(_depthBuffer.ReleaseAndGetAddressOf()));

	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	resdesc.Width = shadow_difinition;//ライトデプス幅
	resdesc.Height = shadow_difinition;//ライトデプス高
	result = _dev->CreateCommittedResource(&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&cv,
		IID_PPV_ARGS(_lightDepthBuffer.ReleaseAndGetAddressOf()));

	if (FAILED(result)) {
		return false;
	}
	return true;

}
bool 
Dx12Wrapper::CreateDSV() {
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NodeMask = 0;
	desc.NumDescriptors = 2;//0番は通常深度、1番はライト深度(影用)
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	auto result=_dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(_dsvHeap.ReleaseAndGetAddressOf()));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}
	D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
	viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	viewDesc.Flags = D3D12_DSV_FLAG_NONE;
	viewDesc.Format = DXGI_FORMAT_D32_FLOAT;

	auto handle=_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	//通常デプス
	_dev->CreateDepthStencilView(_depthBuffer.Get(),
		&viewDesc,
		handle);
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	//ライトデプス
	_dev->CreateDepthStencilView(_lightDepthBuffer.Get(),
		&viewDesc,
		handle);

	return true;
}

void Dx12Wrapper::DrawPrimitiveShapes() {
	auto wsize = Application::Instance().GetWindowSize();

	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT(0.0f, 0.0f, wsize.width, wsize.height);
	_cmdList->RSSetViewports(1, &vp);//ビューポート

	CD3DX12_RECT rc(0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects(1, &rc);//シザー(切り抜き)矩形

	_cmdList->SetGraphicsRootSignature(_primitveRS.Get());
	_cmdList->SetPipelineState(_primitivePipeline.Get());
	_cmdList->SetDescriptorHeaps(1, _depthSRVHeap.GetAddressOf());
	auto handle = _depthSRVHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cmdList->SetGraphicsRootDescriptorTable(3, handle);


	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (int i = 0; i < _primitivesIBV.size(); ++i) {
		_cmdList->IASetIndexBuffer(&_primitivesIBV[i]);
		_cmdList->IASetVertexBuffers(0, 1, &_primitivesVBV[i]);
		_cmdList->DrawIndexedInstanced(_primitivesIBV[i].SizeInBytes/2, 1, 0, 0, 0);
	}

}

//描画
void
Dx12Wrapper::DrawToPera1(shared_ptr<PMDRenderer> renderer) {
	auto wsize = Application::Instance().GetWindowSize();

	



	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT(0.0f, 0.0f, wsize.width, wsize.height);
	_cmdList->RSSetViewports(1, &vp);//ビューポート

	CD3DX12_RECT rc(0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects(1, &rc);//シザー(切り抜き)矩形

	_cmdList->SetDescriptorHeaps(1, _depthSRVHeap.GetAddressOf());
	auto handle = _depthSRVHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cmdList->SetGraphicsRootDescriptorTable(3, handle);

	//_cmdList->SetGraphicsRootSignature(_PRIVILEGE_SET)
	//_cmdList->IASetVertexBuffers(0,1,&_primitivesVBV[0]);
	//_cmdList->IASetIndexBuffer(&_primitivesIBV[0]);
	//_cmdList->DrawIndexedInstanced(4, 1, 0, 0, 0);

}

bool 
Dx12Wrapper::CreatePeraPipeline() {
	D3D12_DESCRIPTOR_RANGE range[4] = {};
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;//b
	range[0].BaseShaderRegister = 0;//0
	range[0].NumDescriptors = 1;

	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;//t
	range[1].BaseShaderRegister = 0;//0
	range[1].NumDescriptors = 1;
	
	range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;//t
	range[2].BaseShaderRegister = 1;//1
	range[2].NumDescriptors = 1;

	//深度値テクスチャ用
	range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;//t
	range[3].BaseShaderRegister = 2;//2
	range[3].NumDescriptors = 2;//2個使うよ(t2,t3)

	D3D12_ROOT_PARAMETER rp[4] = {};
	rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[0].DescriptorTable.pDescriptorRanges = &range[0];
	rp[0].DescriptorTable.NumDescriptorRanges = 1;

	rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[1].DescriptorTable.pDescriptorRanges = &range[1];
	rp[1].DescriptorTable.NumDescriptorRanges = 1;

	rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[2].DescriptorTable.pDescriptorRanges = &range[2];
	rp[2].DescriptorTable.NumDescriptorRanges = 1;

	rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[3].DescriptorTable.pDescriptorRanges = &range[3];
	rp[3].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 4;
	rsDesc.pParameters = rp;
	D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0);
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	rsDesc.pStaticSamplers = &sampler;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	

	ComPtr<ID3DBlob> rsBlob;
	ComPtr<ID3DBlob> errBlob;
	auto result=D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, rsBlob.ReleaseAndGetAddressOf(), errBlob.ReleaseAndGetAddressOf());
	if (!CheckResult(result, errBlob.Get())) {
		assert(0);
		return false;
	}
	result = _dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(_peraRS.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	ComPtr<ID3DBlob> vs;
	ComPtr<ID3DBlob> ps;
	result = D3DCompileFromFile(L"PeraVertexShader.hlsl", 
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, 
		"PeraVS", "vs_5_0", 0, 0, vs.ReleaseAndGetAddressOf(), errBlob.ReleaseAndGetAddressOf());
	if (!CheckResult(result,errBlob.Get())) {
		assert(0);
		return false;
	}
	result = D3DCompileFromFile(L"PeraPixelShader.hlsl", 
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, 
		"PeraPS", "ps_5_0", 0, 0, ps.ReleaseAndGetAddressOf(), errBlob.ReleaseAndGetAddressOf());
	if (!CheckResult(result, errBlob.Get())) {
		assert(0);
		return false;
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc = {};
	gpsDesc.pRootSignature = _peraRS.Get();
	gpsDesc.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
	gpsDesc.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
	gpsDesc.DepthStencilState.DepthEnable = false;
	gpsDesc.DepthStencilState.StencilEnable = false;
	D3D12_INPUT_ELEMENT_DESC layout[2] = {
		{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	};
	gpsDesc.InputLayout.NumElements = _countof(layout);
	gpsDesc.InputLayout.pInputElementDescs = layout;
	gpsDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gpsDesc.BlendState.RenderTarget[0].BlendEnable = true;
	gpsDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	gpsDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	gpsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpsDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpsDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpsDesc.SampleDesc.Count = 1;
	gpsDesc.SampleDesc.Quality = 0;
	gpsDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	result = _dev->CreateGraphicsPipelineState(&gpsDesc, IID_PPV_ARGS(_peraPipeline.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	result = D3DCompileFromFile(L"PeraPixelShader.hlsl", 
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, 
		"VerticalBlurPS", "ps_5_0", 0, 0, ps.ReleaseAndGetAddressOf(), errBlob.ReleaseAndGetAddressOf());
	if (!CheckResult(result, errBlob.Get())) {
		assert(0);
		return false;
	}
	gpsDesc.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
	result = _dev->CreateGraphicsPipelineState(&gpsDesc, IID_PPV_ARGS(_peraPipeline2.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}
	return true;
}

//描画
void 
Dx12Wrapper::Draw(shared_ptr<PMDRenderer> renderer) {
	auto wsize = Application::Instance().GetWindowSize();
	
	D3D12_VIEWPORT vp=CD3DX12_VIEWPORT(0.0f,0.0f,wsize.width,wsize.height);
	_cmdList->RSSetViewports(1, &vp);//ビューポート

	CD3DX12_RECT rc(0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects(1,&rc);//シザー(切り抜き)矩形

	_cmdList->SetGraphicsRootSignature(_peraRS.Get());
	_cmdList->SetDescriptorHeaps(1, _peraSRVHeap.GetAddressOf());
	auto handle=_peraSRVHeap->GetGPUDescriptorHandleForHeapStart();
	//最終表示物はペラ２枚目なので、インクリメントする
	//handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cmdList->SetGraphicsRootDescriptorTable(1, handle);
	
	_cmdList->SetDescriptorHeaps(1, _peraCBVHeap.GetAddressOf());
	_cmdList->SetGraphicsRootDescriptorTable(0, _peraCBVHeap->GetGPUDescriptorHandleForHeapStart());

	_cmdList->SetDescriptorHeaps(1, _distSRVHeap.GetAddressOf());
	_cmdList->SetGraphicsRootDescriptorTable(2, _distSRVHeap->GetGPUDescriptorHandleForHeapStart());

	//深度バッファテクスチャ
	_cmdList->SetDescriptorHeaps(1, _depthSRVHeap.GetAddressOf());
	_cmdList->SetGraphicsRootDescriptorTable(3, _depthSRVHeap->GetGPUDescriptorHandleForHeapStart());


	_cmdList->SetPipelineState(_peraPipeline.Get());
	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_cmdList->IASetVertexBuffers(0, 1, &_peraVBV);
	_cmdList->DrawInstanced(4, 1, 0, 0);
}

void Dx12Wrapper::SetCameraSetting()
{
	auto wsize = Application::Instance().GetWindowSize();

	auto eyePos = XMLoadFloat3(&_eye);
	auto targetPos = XMLoadFloat3(&_target);
	auto upVec = XMLoadFloat3(&_up);

	_mappedScene->eye = _eye;
	_mappedScene->view = XMMatrixLookAtLH(eyePos,targetPos,upVec);
	_mappedScene->proj = XMMatrixPerspectiveFovLH(
		_fov,
		static_cast<float>(wsize.width) / static_cast<float>(wsize.height),
		1.0f,
		100.0f);
	auto plane = XMFLOAT4(0, 1, 0, 0);//平面
	XMVECTOR planeVec = XMLoadFloat4(&plane);
	auto light = XMFLOAT4(-1, 1, -1, 0);
	XMVECTOR lightVec = XMLoadFloat4(&light);
	_mappedScene->shadow = XMMatrixShadow(planeVec, lightVec);

	float armLength;//カメラアーム長(視点から注視点の長さ)
	armLength=XMVector3Length(XMVectorSubtract( targetPos, eyePos)).m128_f32[0];
	auto lightPos= targetPos+XMVector3Normalize(lightVec)*XMVector3Length(XMVectorSubtract(targetPos, eyePos)).m128_f32[0];
	_mappedScene->lightCamera = XMMatrixLookAtLH(lightPos, targetPos, upVec)*
		//XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280.0f/720.0f, 1.0f, 100.0f);
		XMMatrixOrthographicLH(40,40,1.0f,100.0f);
}

bool 
Dx12Wrapper::CreatePrimitives() {


	constexpr XMFLOAT3 center(0, 0, 0);
	constexpr float width=40.0f;
	constexpr float height=40.0f;

	PrimitiveVertex plane[4] = {
		{{center.x - width / 2.0f, center.y,center.z - height / 2.0f},{0,1,0}},
		{{center.x - width / 2.0f, center.y,center.z + height / 2.0f},{0,1,0}},
		{{center.x + width / 2.0f, center.y,center.z - height / 2.0f},{0,1,0}},
		{{center.x + width / 2.0f, center.y,center.z + height / 2.0f},{0,1,0}},
	};
	uint16_t indexes[]={ 0,1,2,1,3,2 };

	//頂点バッファ作成
	ID3D12Resource* vbuff=nullptr;
	auto result = _dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(plane)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vbuff));
	_primitivesVB.push_back(vbuff);
	if (!CheckResult(result)) {
		return false;
	}
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vbuff->GetGPUVirtualAddress();
	vbv.SizeInBytes = sizeof(plane);
	vbv.StrideInBytes = sizeof(PrimitiveVertex);
	_primitivesVBV.push_back(vbv);


	PrimitiveVertex* mappedVertex=nullptr;
	//マップしてコピー
	vbuff->Map(0, nullptr, (void**)&mappedVertex);
	copy(begin(plane), end(plane), mappedVertex);
	vbuff->Unmap(0, nullptr);

	//インデックスバッファ作成
	ID3D12Resource* ibuff = nullptr;
	result = _dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(indexes)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&ibuff));
	_primitivesIB.push_back(ibuff);
	if (!CheckResult(result)) {
		return false;
	}
	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = ibuff->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = sizeof(indexes);
	_primitivesIBV.push_back(ibv);

	uint16_t* mappedIndex;

	ibuff->Map(0, nullptr, (void**)&mappedIndex);
	copy(begin(indexes), end(indexes), mappedIndex);
	ibuff->Unmap(0, nullptr);

	constexpr uint32_t div = 16;

	PrimitiveVertex cone[div*3+1];//最後のはフタぶん
	float radius = 5.0f;
	auto angle = 0.0f;
	constexpr float cylinder_height = 8.0f;
	//上段
	for (int i = 0; i < div; ++i) {
		
		cone[i].pos.x = cos(angle)*radius;
		cone[i].pos.y = cylinder_height;
		cone[i].pos.z = sin(angle)*radius;

		cone[i].normal.x = cos(angle);
		cone[i].normal.y = 0;
		cone[i].normal.z = sin(angle);
		angle += XM_2PI / static_cast<float>(div);
	}
	//下段
	for (int i = 0; i < div; ++i) {

		cone[i + div].pos.x = cos(angle)*radius;
		cone[i + div].pos.y = 0;
		cone[i + div].pos.z = sin(angle)*radius;

		cone[i + div].normal.x = cos(angle);
		cone[i + div].normal.y = 0;
		cone[i + div].normal.z = sin(angle);
		angle += XM_2PI / static_cast<float>(div);
	}
	//フタ
	for (int i = 0; i < div; ++i) {

		cone[div*2+i].pos.x = cos(angle)*radius;
		cone[div * 2 + i].pos.y = cylinder_height;
		cone[div * 2 + i].pos.z = sin(angle)*radius;

		cone[div * 2 + i].normal.x = 0;
		cone[div * 2 + i].normal.y = 1;
		cone[div * 2 + i].normal.z = 0;
		angle += XM_2PI / static_cast<float>(div);
	}
	cone[div * 3].pos = XMFLOAT3(0, cylinder_height, 0);
	cone[div * 3].normal = XMFLOAT3(0, 1, 0);

	uint16_t coneIdxes[div * (3*(2+1))];
	uint16_t idx = 0;
	for (int i = 0; i < div; ++i) {
		//上段
		coneIdxes[idx] = i;
		++idx;
		coneIdxes[idx] = (i+1)%div;
		++idx;
		coneIdxes[idx] = (i+1)%div+div;
		++idx;
		//下段
		coneIdxes[idx] = i+div;
		++idx;
		coneIdxes[idx] = i;
		++idx;
		coneIdxes[idx] = (i + 1) % div + div;
		++idx;
	}
	//フタ
	for (int i = 0; i < div; ++i) {
		coneIdxes[idx] = div * 3;
		++idx;
		coneIdxes[idx] = i + div * 2;
		++idx;
		coneIdxes[idx] = (i+1)%div+div*2;
		++idx;
	}

	//頂点バッファ作成
	ID3D12Resource* coneVBuff = nullptr;
	result = _dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(cone)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&coneVBuff));
	_primitivesVB.push_back(coneVBuff);
	if (!CheckResult(result)) {
		return false;
	}
	vbv = {};
	vbv.BufferLocation = coneVBuff->GetGPUVirtualAddress();
	vbv.SizeInBytes = sizeof(cone);
	vbv.StrideInBytes = sizeof(PrimitiveVertex);
	_primitivesVBV.push_back(vbv);

	

	//インデックスバッファ作成
	ID3D12Resource* coneIB = nullptr;
	result = _dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(coneIdxes)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&coneIB));
	_primitivesIB.push_back(ibuff);
	if (!CheckResult(result)) {
		return false;
	}
	ibv = {};
	ibv.BufferLocation = coneIB->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = sizeof(coneIdxes);
	_primitivesIBV.push_back(ibv);

	mappedVertex = nullptr;
	//マップしてコピー
	coneVBuff->Map(0, nullptr, (void**)&mappedVertex);
	copy(begin(cone), end(cone), mappedVertex);
	coneVBuff->Unmap(0, nullptr);

	mappedIndex=nullptr;

	coneIB->Map(0, nullptr, (void**)&mappedIndex);
	copy(begin(coneIdxes), end(coneIdxes), mappedIndex);
	coneIB->Unmap(0, nullptr);

	return true;
}

bool
Dx12Wrapper::CreatePrimitiveRootSignature() {
	D3D12_DESCRIPTOR_RANGE range[5] = {};
	//レンジ0は座標変換(カメラとワールドのふたつ)b1
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;//b
	range[0].BaseShaderRegister = 1;//1
	range[0].NumDescriptors = 1;
	range[0].OffsetInDescriptorsFromTableStart = 0;
	range[0].RegisterSpace = 0;

	//レンジ1はマテリアル
	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;//b
	range[1].BaseShaderRegister = 0;//0
	range[1].NumDescriptors = 1;
	range[1].OffsetInDescriptorsFromTableStart = 0;
	range[1].RegisterSpace = 0;

	//レンジ２はテクスチャ
	range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;//t
	range[2].BaseShaderRegister = 0;//0
	range[2].NumDescriptors = 4;
	range[2].OffsetInDescriptorsFromTableStart = 1;
	range[2].RegisterSpace = 0;

	//レンジ3はアクターの座標変換(ワールドとボーン)
	range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;//b
	range[3].BaseShaderRegister = 2;//2
	range[3].NumDescriptors = 2;
	range[3].OffsetInDescriptorsFromTableStart = 0;
	range[3].RegisterSpace = 0;

	range[4] = CD3DX12_DESCRIPTOR_RANGE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

	CD3DX12_ROOT_PARAMETER rp[4] = {};
	//マテリアル用(テクスチャも含む)
	rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[0].DescriptorTable.NumDescriptorRanges = 2;
	rp[0].DescriptorTable.pDescriptorRanges = &range[1];

	//カメラ座標変換用
	rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rp[1].DescriptorTable.NumDescriptorRanges = 1;
	rp[1].DescriptorTable.pDescriptorRanges = &range[0];

	//アクター座標変換用
	rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rp[2].DescriptorTable.NumDescriptorRanges = 1;
	rp[2].DescriptorTable.pDescriptorRanges = &range[3];

	rp[3].InitAsDescriptorTable(1, &range[4]);


	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rsDesc.NumParameters = 4;
	rsDesc.pParameters = rp;

	D3D12_STATIC_SAMPLER_DESC sampler[3] = {};
	sampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	sampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler[0].MaxAnisotropy = 0;
	sampler[0].MaxLOD = D3D12_FLOAT32_MAX;
	sampler[0].MinLOD = 0;
	sampler[0].MipLODBias = 0.0f;
	sampler[0].ShaderRegister = 0;
	sampler[0].RegisterSpace = 0;

	sampler[1] = sampler[0];
	sampler[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler[1].ShaderRegister = 1;

	sampler[2] = sampler[0];
	sampler[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	sampler[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	sampler[2].MinLOD = -D3D12_FLOAT32_MAX;
	sampler[2].ShaderRegister = 2;
	sampler[2].MaxAnisotropy = 1;


	rsDesc.NumStaticSamplers = 3;
	rsDesc.pStaticSamplers = sampler;
	ID3DBlob* rsBlob = nullptr;//ルートシグネチャを構成するためのブロブ
	ID3DBlob* errBlob = nullptr;
	auto result = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &errBlob);
	if (!CheckResult(result, errBlob)) {
		return false;
	}

	result = _dev->CreateRootSignature(0, //ノードマスク
		rsBlob->GetBufferPointer(), //ルートシグネチャを作るためのバイナリ
		rsBlob->GetBufferSize(),// ↑のサイズ
		IID_PPV_ARGS(_primitveRS.ReleaseAndGetAddressOf()));
	if (!CheckResult(result, errBlob)) {
		return false;
	}
	return true;
}

bool 
Dx12Wrapper::CreatePrimitivePipeline() {
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* errBlob = nullptr;
	auto result = D3DCompileFromFile(
		L"BasicVertexShader.hlsl",//シェーダファイルパス
		nullptr,//マクロ定義(D3D_SHADER_MACROのポインタ)
		D3D_COMPILE_STANDARD_FILE_INCLUDE,//インクルード定義(ID3DIncludeのポインタ)
		"PrimitiveVS",//エントリポイント関数名
		"vs_5_0",//シェーダのバージョン
		0,//フラグ(あまり意味ない)
		0,//フラグ(あまり意味ない)
		&vsBlob,//シェーダコードをコンパイルした塊が入る
		&errBlob);//これもBLOB(塊)なんだけど、実はエラー文字列が入る
	if (!CheckResult(result, errBlob)) {
		return false;
	}

	ID3DBlob* psBlob = nullptr;
	result = D3DCompileFromFile(
		L"BasicPixelShader.hlsl",
		nullptr, 
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"PrimitivePS", "ps_5_0",
		0, 0, &psBlob, &errBlob);
	if (!CheckResult(result, errBlob)) {
		return false;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC plsDesc = {};

	//頂点の仕様
	D3D12_INPUT_ELEMENT_DESC inputdesc[] = {
		{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	};

	plsDesc.InputLayout.NumElements = _countof(inputdesc);
	plsDesc.InputLayout.pInputElementDescs = inputdesc;
	plsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;


	//頂点シェーダ
	plsDesc.VS = CD3DX12_SHADER_BYTECODE(vsBlob);

	//ラスタライザ
	plsDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	plsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//カリング

	//ピクセルシェーダ
	plsDesc.PS = CD3DX12_SHADER_BYTECODE(psBlob);


	//出力
	plsDesc.NumRenderTargets = 1;//レンダーターゲット数
	//↑で指定したレンダーターゲット数は「必ず」設定しなければ
	//ならない↓
	plsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	plsDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	plsDesc.BlendState.RenderTarget[0].BlendEnable = false;//今のところfalse
	plsDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	plsDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	//SrcBlend=SRC_ALPHAというのはSrc*αということ
	plsDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	plsDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	plsDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	plsDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;



	//デプスステンシルステート
	//今はデプスとか使用しない(ソリッドモデル時に必要)
	plsDesc.DepthStencilState.DepthEnable = true;
	plsDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	plsDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	plsDesc.DepthStencilState.StencilEnable = false;
	plsDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	//全体的な設定
	plsDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	plsDesc.NodeMask = 0;
	plsDesc.SampleDesc.Count = 1;
	plsDesc.SampleDesc.Quality = 0;
	plsDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	plsDesc.pRootSignature = _primitveRS.Get();

	result = _dev->CreateGraphicsPipelineState(&plsDesc, IID_PPV_ARGS(_primitivePipeline.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		return false;
	}

	return true;

}

bool
Dx12Wrapper::CreateTextureFromImageData(const DirectX::Image* img,ComPtr<ID3D12Resource>& buff,bool isDiscrete) {
	//まずWriteToSubresource方式で考える。
	D3D12_HEAP_PROPERTIES heapprop = {};

	if (isDiscrete) {
		//グラボがディスクリートの場合はDEFAULTで作っておいて
		//後から作るUPLOADバッファを中間バッファ(UPLOAD)として用い
		//CopyTextureRegionで転送する
		heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	}
	else {//一体型(UMA)
		heapprop.Type = D3D12_HEAP_TYPE_CUSTOM;//テクスチャ用
		heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		heapprop.CreationNodeMask = 0;
		heapprop.VisibleNodeMask = 0;
	}

	//最終書き込み先リソースの設定
	D3D12_RESOURCE_DESC resdesc =
	CD3DX12_RESOURCE_DESC::Tex2D(img->format,img->width,img->height);

	auto result = S_OK;
	if (isDiscrete) {
		result = _dev->CreateCommittedResource(&heapprop,
			D3D12_HEAP_FLAG_NONE,
			&resdesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(buff.ReleaseAndGetAddressOf()));
	}else{
		result = _dev->CreateCommittedResource(&heapprop,
			D3D12_HEAP_FLAG_NONE,
			&resdesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(buff.ReleaseAndGetAddressOf()));
	}

	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	return true;
}

bool 
Dx12Wrapper::CreateWhiteTexture() {
	D3D12_HEAP_PROPERTIES heapprop = {};
	heapprop.Type = D3D12_HEAP_TYPE_CUSTOM;//テクスチャ用
	heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapprop.CreationNodeMask = 0;
	heapprop.VisibleNodeMask = 0;
	
	D3D12_RESOURCE_DESC resdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4);

	auto result = _dev->CreateCommittedResource(&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(_whiteTex.ReleaseAndGetAddressOf()));

	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	////白
	vector<uint8_t> col(4*4*4);
	fill(col.begin(), col.end(), 0xff);
	result = _whiteTex->WriteToSubresource(0, nullptr, col.data(),4*4,col.size());
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	return true;

}

bool
Dx12Wrapper::CreateBlackTexture() {
	D3D12_HEAP_PROPERTIES heapprop = {};
	heapprop.Type = D3D12_HEAP_TYPE_CUSTOM;//テクスチャ用
	heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapprop.CreationNodeMask = 0;
	heapprop.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC resdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4);
	
	auto result = _dev->CreateCommittedResource(&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(_blackTex.ReleaseAndGetAddressOf()));

	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	//黒
	vector<uint8_t> col(4 * 4 * 4);
	fill(col.begin(), col.end(), 0);
	result = _blackTex->WriteToSubresource(0, nullptr, col.data(), 4 * 4, col.size());
	if (!CheckResult(result)) {
		return false;
	}
	return true;
}

bool
Dx12Wrapper::CreateGradationTexture() {
	D3D12_HEAP_PROPERTIES heapprop = {};
	heapprop.Type = D3D12_HEAP_TYPE_CUSTOM;//テクスチャ用
	heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapprop.CreationNodeMask = 0;
	heapprop.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC resdesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 256);

	auto result = _dev->CreateCommittedResource(&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(_gradTex.ReleaseAndGetAddressOf()));

	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}
	struct Color {
		uint8_t r, g, b, a;
		Color(uint8_t inr,
			uint8_t ing, 
			uint8_t inb, 
			uint8_t ina ):r(inr),
			g(ing) ,
			b(inb) ,
			a(ina) {}
		Color() {}
	};
	//白→黒グラデ
	vector<Color> col(4 * 256);
	auto it = col.begin();
	for (int i = 255; i >=0; --i) {
		fill(it,it+4, Color(i,i,i,255));
		it += 4;
	}
	result = _gradTex->WriteToSubresource(0, nullptr, col.data(), sizeof(Color) * 4, sizeof(Color)*col.size());
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	return true;
}


bool 
Dx12Wrapper::CreateDistortion() {
	if (!LoadPictureFromFile(L"normal/normalmap.jpg", _distBuff)) {
		return false;
	}
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	auto result=_dev->CreateDescriptorHeap(&heapDesc,
		IID_PPV_ARGS(&_distSRVHeap));

	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC resDesc = {};
	resDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	resDesc.Format = _distBuff->GetDesc().Format;
	resDesc.Texture2D.MipLevels = 1;
	resDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	//あとはビューを作る
	_dev->CreateShaderResourceView(
		_distBuff.Get(),
		&resDesc,
		_distSRVHeap->GetCPUDescriptorHandleForHeapStart());
	return true;

}

XMVECTOR 
Dx12Wrapper::GetCameraPosition() {
	return XMLoadFloat3(&_eye);
}

bool
Dx12Wrapper::LoadPictureFromFile(wstring filepath, ComPtr<ID3D12Resource>& buff) {

	//同じパス指定が来たらマップにすでに存在する
	//データから受け取る(フライウェイトパターン)
	auto it = _textureTable.find(filepath);
	if (it != _textureTable.end()) {
		buff = _textureTable[filepath];
		return true;
	}


	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	HRESULT result = S_OK;
	//DXT圧縮の際には特殊な処理が必要
	//(4x4ピクセル１ブロックとなるため高さ1/4になる)
	bool isDXT=false;
	auto ext = GetExtension(filepath);
	if (ext == L"tga") {
		result = LoadFromTGAFile(filepath.c_str(),
			&metadata,
			scratchImg);
	}
	else if (ext == L"dds") {
		result = LoadFromDDSFile(filepath.c_str(),0,
			&metadata,
			scratchImg);
		isDXT = true;
	}
	else {
		result = LoadFromWICFile(filepath.c_str(),
			0,
			&metadata,
			scratchImg);
	}
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}
	auto img = scratchImg.GetImage(0,0,0);
	
	bool isDescrete = true;

	if (!CreateTextureFromImageData(img,buff, isDescrete)) {
		return false;
	}

	if (!isDescrete) {
		result = buff->WriteToSubresource(0, 
			nullptr, 
			img->pixels, 
			img->rowPitch, 
			img->slicePitch);
	}
	else {
		
		//転送のための中間バッファを作成する
		D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		
		D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(
			AligmentedValue(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)*img->height);
		Microsoft::WRL::ComPtr<ID3D12Resource> internalBuffer = nullptr;
		result = _dev->CreateCommittedResource(&heapProp,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(internalBuffer.ReleaseAndGetAddressOf()));
		assert(SUCCEEDED(result));
		if (FAILED(result)) {
			return false;
		}
		uint8_t* mappedInternal = nullptr;
		internalBuffer->Map(0, nullptr, (void**)&mappedInternal);
		auto address = img->pixels;
		uint32_t height = isDXT ? img->height/4 : img->height;
		for (int i = 0; i < height; ++i) {
			copy_n(address, img->rowPitch, mappedInternal);
			address += img->rowPitch;
			mappedInternal += AligmentedValue(img->rowPitch,
				D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		}
		internalBuffer->Unmap(0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.pResource = internalBuffer.Get();
		src.PlacedFootprint.Offset = 0;
		src.PlacedFootprint.Footprint.Width = img->width;
		src.PlacedFootprint.Footprint.Height = img->height;
		src.PlacedFootprint.Footprint.RowPitch =
			AligmentedValue(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		src.PlacedFootprint.Footprint.Depth = metadata.depth;
		src.PlacedFootprint.Footprint.Format = img->format;

		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		dst.pResource = buff.Get();

		_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		Barrier(buff.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		_cmdList->Close();
		ID3D12CommandList* cmds[] = { _cmdList.Get() };
		_cmdQue->ExecuteCommandLists(1, cmds);
		WaitForCommandQueue();
		_cmdAlloc->Reset();
		_cmdList->Reset(_cmdAlloc.Get(), nullptr);
	}
	
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	_textureTable[filepath] = buff;

	return SUCCEEDED(result);
}
bool 
Dx12Wrapper::CreateTransformConstantBuffer() {
	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(
		AligmentedValue(sizeof(SceneMatrix), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
	);

	auto result = _dev->CreateCommittedResource(&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(_sceneCB.ReleaseAndGetAddressOf()));

	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return false;
	}

	return true;
}

bool 
Dx12Wrapper::CreateTransformBufferView()
{
	//定数バッファビューの作成
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	auto result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(_sceneHeap.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		return false;
	}

	auto handle = _sceneHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc = {};
	viewDesc.BufferLocation = _sceneCB->GetGPUVirtualAddress();
	viewDesc.SizeInBytes = _sceneCB->GetDesc().Width;
	_dev->CreateConstantBufferView(&viewDesc, handle);

	_sceneCB->Map(0, nullptr, (void**)&_mappedScene);
	SetCameraSetting();
	
	return true;
}

bool 
Dx12Wrapper::CreatePera1ResourceAndView() {
	auto& bbuff=_backBuffers[0];
	auto resDesc=bbuff->GetDesc();
	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Color[0] = clearValue.Color[1] = clearValue.Color[2] = 0.5f;
	clearValue.Color[3] = 1.0f;
	clearValue.Format = resDesc.Format;
	auto result = _dev->CreateCommittedResource(&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(_peraResource.ReleaseAndGetAddressOf()));

	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	//もともと作っているヒープの情報でもう一枚作る
	auto heapDesc = _rtvDescHeap->GetDesc();
	//レンダーターゲットビュー(RTV)を作る
	//ただしその前にでスクリプタヒープが必要
	heapDesc.NumDescriptors = 2;
	result = _dev->CreateDescriptorHeap(&heapDesc, 
		IID_PPV_ARGS(_peraRTVHeap.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//まだデスクリプタヒープのみで、ビューを作っていないので作る
	//レンダーターゲットビューを２枚作る
	auto handle = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart();
	_dev->CreateRenderTargetView(_peraResource.Get(),
		&rtvDesc, handle);
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	_dev->CreateRenderTargetView(_peraResource2.Get(),
		&rtvDesc, handle);

	//シェーダリソースビュービューを作る
	//ただしその前にでスクリプタヒープが必要
	heapDesc.NumDescriptors = 2;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	
	result = _dev->CreateDescriptorHeap(&heapDesc,
		IID_PPV_ARGS(_peraSRVHeap.ReleaseAndGetAddressOf()));



	if (!CheckResult(result)) {
		assert(0);
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = rtvDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	handle = _peraSRVHeap->GetCPUDescriptorHandleForHeapStart();
	_dev->CreateShaderResourceView(_peraResource.Get(),
		&srvDesc,
		handle);
	handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_dev->CreateShaderResourceView(_peraResource2.Get(),
		&srvDesc,
		handle);
	return true;

}


// ペラポリ２枚目用
bool 
Dx12Wrapper::CreatePera2Resource()
{
	auto wsize=Application::Instance().GetWindowSize();
	auto resDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,
		wsize.width,
		wsize.height);
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Color[0] = clearValue.Color[1] = clearValue.Color[2] = 0.5f;
	clearValue.Color[3]=1.0f;
	clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	auto result = _dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(_peraResource2.ReleaseAndGetAddressOf()));
	if (!CheckResult(result)) {
		assert(0);
		return false;
	}



	return true;
}

void 
Dx12Wrapper::DrawToPera2() {
	Barrier(_peraResource2.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	auto rtvHeapPointer = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHeapPointer.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	_cmdList->OMSetRenderTargets(1, &rtvHeapPointer, false, nullptr);

	auto wsize = Application::Instance().GetWindowSize();

	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT(0.0f, 0.0f, wsize.width, wsize.height);
	_cmdList->RSSetViewports(1, &vp);//ビューポート

	CD3DX12_RECT rc(0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects(1, &rc);//シザー(切り抜き)矩形

	_cmdList->SetGraphicsRootSignature(_peraRS.Get());
	_cmdList->SetDescriptorHeaps(1, _peraSRVHeap.GetAddressOf());

	auto handle=_peraSRVHeap->GetGPUDescriptorHandleForHeapStart();
	_cmdList->SetGraphicsRootDescriptorTable(1, handle);

	_cmdList->SetDescriptorHeaps(1, _peraCBVHeap.GetAddressOf());
	_cmdList->SetGraphicsRootDescriptorTable(0, _peraCBVHeap->GetGPUDescriptorHandleForHeapStart());

	_cmdList->SetPipelineState(_peraPipeline.Get());
	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_cmdList->IASetVertexBuffers(0, 1, &_peraVBV);
	_cmdList->DrawInstanced(4, 1, 0, 0);

	Barrier(_peraResource2.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

}