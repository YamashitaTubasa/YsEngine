#include "FbxModel.h"

FbxModel::~FbxModel()
{
	// FBXシーンの解放
	fbxScene->Destroy();
}

void FbxModel::CreateBuffers(ID3D12Device* device)
{
	HRESULT result;
	
	// 頂点データ全体のサイズ
	UINT sizeVB = static_cast<UINT>(sizeof(VertexPosNormalUvSkin) * vertices.size());

	// ヒープ設定
	CD3DX12_HEAP_PROPERTIES verHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	// リソース設定
	CD3DX12_RESOURCE_DESC verResDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeVB);

	// 頂点バッファ生成
	result = device->CreateCommittedResource(
		&verHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&verResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertBuff));

	// 頂点バッファへのデータ転送
	VertexPosNormalUvSkin* vertMap = nullptr;
	result = vertBuff->Map(0, nullptr, (void**)&vertMap);
	if (SUCCEEDED(result)) {
		std::copy(vertices.begin(), vertices.end(), vertMap);
		vertBuff->Unmap(0, nullptr);
	}

	// 頂点バッファビュー(VBV)の生成
	vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
	vbView.SizeInBytes = sizeVB;
	vbView.StrideInBytes = sizeof(vertices[0]);

	// 頂点インデックス全体のサイズ
	UINT sizeIB = static_cast<UINT>(sizeof(unsigned short) * indices.size());

	// ヒープ設定
	CD3DX12_HEAP_PROPERTIES inHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	// リソース設定
	CD3DX12_RESOURCE_DESC inResDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeIB);

	// インデックスバッファの生成
	result = device->CreateCommittedResource(
		&inHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&inResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuff));

	// インデックスバッファへのデータ転送
	unsigned short* indexMap = nullptr;
	result = indexBuff->Map(0, nullptr, (void**)&indexMap);
	if (SUCCEEDED(result)) {
		std::copy(indices.begin(), indices.end(), indexMap);
		indexBuff->Unmap(0, nullptr);
	}

	// インデックスバッファビュー(IBV)の生成
	ibView.BufferLocation = indexBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = sizeIB;

	// テクスチャ画像データ
	const DirectX::Image* img = scratchImg.GetImage(0, 0, 0); // 生データ抽出
	assert(img);

	// ヒープ設定
	CD3DX12_HEAP_PROPERTIES texHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);
	// リソース設定
	CD3DX12_RESOURCE_DESC texresDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		metadata.width,
		(UINT)metadata.height,
		(UINT16)metadata.arraySize,
		(UINT16)metadata.mipLevels
	);

	// テクスチャ用バッファの生成
	result = device->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&texresDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // テクスチャ用設定
		nullptr,
		IID_PPV_ARGS(&texbuff));

	// テクスチャバッファにデータ転送
	result = texbuff->WriteToSubresource(
		0,
		nullptr, // 全領域へのコピー
		img->pixels, // 元データアドレス
		(UINT)img->rowPitch, // 1ラインサイズ
		(UINT)img->slicePitch // 1枚サイズ
	);

	// SRV用デスクリプタヒープを生成
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // シェーダから見えるように
	descHeapDesc.NumDescriptors = 1; // テクスチャ枚数
	result = device->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&descHeapSRV)); // 生成

	// シェーダリソースビュー(SRV)作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	D3D12_RESOURCE_DESC resDesc = texbuff->GetDesc();

	srvDesc.Format = resDesc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
	srvDesc.Texture1D.MipLevels = 1;

	device->CreateShaderResourceView(texbuff.Get(), // ビューと関連付けるバッファ
		&srvDesc, // テクスチャ設定情報
		descHeapSRV->GetCPUDescriptorHandleForHeapStart() // ヒープの先頭アドレス
	);
}

void FbxModel::Draw(ID3D12GraphicsCommandList* cmdList) 
{
	// 頂点バッファをセット(CBV)
	cmdList->IASetVertexBuffers(0, 1, &vbView);
	// インデックスバッファをセット(IVB)
	cmdList->IASetIndexBuffer(&ibView);

	// デスクリプタヒープのセット
	ID3D12DescriptorHeap* ppHeaps[] = { descHeapSRV.Get() };
	cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	// シェーダリソースビューをセット
	cmdList->SetGraphicsRootDescriptorTable(1, descHeapSRV->GetGPUDescriptorHandleForHeapStart());

	// 描画コマンド
	cmdList->DrawIndexedInstanced((UINT)indices.size(), 1, 0, 0, 0);
}