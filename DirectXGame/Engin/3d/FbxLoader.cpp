﻿#include "FbxLoader.h"

using namespace DirectX;

/// <summary>
/// 静的メンバ変数の実体
/// </summary>
const std::string FbxLoader::baseDirectory = "Resources/";
const std::string FbxLoader::defaultTextureFileName = "Image/White1x1.png";

FbxLoader* FbxLoader::GetInstance()
{
    static FbxLoader instance;

    return &instance;
}

void FbxLoader::Initialize(ID3D12Device* device)
{
    // 再初期化チェック
    assert(fbxManager == nullptr);
    // 引数からメンバ変数に代入
    this->device = device;
    // FBXマネージャの生成
    fbxManager = FbxManager::Create();
    // FBXマネージャの入出力設定
    FbxIOSettings* ios = FbxIOSettings::Create(fbxManager, IOSROOT);
    fbxManager->SetIOSettings(ios);
    // FBXインポータの生成
    fbxImporter = FbxImporter::Create(fbxManager, "");
}

void FbxLoader::Finalize()
{
    // 各種FBXインスタンスの破壊
    fbxImporter->Destroy();
    fbxManager->Destroy();
}

FbxModel* FbxLoader::LoadModelFromFile(const string& modelName)
{
    // モデルと同じ名前のフォルダから読み込む
    const string directoryPath = baseDirectory + modelName + "/";
    // 拡張子.fbxを付加
    const string fileName = modelName + ".fbx";
    // 連結してフルパスを得る
    const string fullpath = directoryPath + fileName;

    // ファイル名を指定してFBXファイルを読み込む
    if (!fbxImporter->Initialize(fullpath.c_str(), -1, fbxManager->GetIOSettings())) {
        assert(0);
    }

    // シーン生成
    FbxScene* fbxScene = FbxScene::Create(fbxManager, "fbxScene");

    // ファイルからロードしたFBXの情報をシーンにインポート
    fbxImporter->Import(fbxScene);

    // モデル生成
    FbxModel* fbxModel = new FbxModel();
    fbxModel->name = modelName;
    // FBXノードの数を取得
    int nodeCount = fbxScene->GetNodeCount();
    // あらかじめ必要数分のメモリを確保することで、アドレスがずれるのを予防
    fbxModel->nodes.reserve(nodeCount);
    // ルートノードから順に解放してモデルに流し込む
    ParseNodeRecursive(fbxModel, fbxScene->GetRootNode());
    // FBXシーン解放
    fbxModel->fbxScene = fbxScene;

    // バッファ生成
    fbxModel->CreateBuffers(device);

    return fbxModel;
}

void FbxLoader::ParseNodeRecursive(FbxModel* fbxModel, FbxNode* fbxNode, Node* parent)
{
    // ノード名を取得
    string name = fbxNode->GetName();
    // モデルにノードを追加(Todo)
    fbxModel->nodes.emplace_back();
    Node& node = fbxModel->nodes.back();
    // ノード名を取得
    node.name = fbxNode->GetName();

    // FBXノードのローカル移動情報
    FbxDouble3 rotation = fbxNode->LclRotation.Get();
    FbxDouble3 scaling = fbxNode->LclScaling.Get();
    FbxDouble3 translation = fbxNode->LclTranslation.Get();

    // 形式変換して代入
    node.rotation = { (float)rotation[0], (float)rotation[1], (float)rotation[2], 0.0f };
    node.scaling = { (float)scaling[0], (float)scaling[1], (float)scaling[2],0.0f };
    node.translation = { (float)translation[0], (float)translation[1],(float)translation[2], 1.0f };

    // 回転角をDegree(度)からラジアンに変換
    node.rotation.m128_f32[0] = XMConvertToRadians(node.rotation.m128_f32[0]);
    node.rotation.m128_f32[1] = XMConvertToRadians(node.rotation.m128_f32[1]);
    node.rotation.m128_f32[2] = XMConvertToRadians(node.rotation.m128_f32[2]);

    // スケール、回転、平行移動行列の計算
    XMMATRIX matScaling, matRotation, matTranslation;
    matScaling = XMMatrixScalingFromVector(node.scaling);
    matRotation = XMMatrixRotationRollPitchYawFromVector(node.rotation);
    matTranslation = XMMatrixTranslationFromVector(node.translation);

    // ローカル変形行列の計算
    node.transform = XMMatrixIdentity();
    node.transform *= matScaling; // ワールド行列にスケーリングを反映
    node.transform *= matRotation; // ワールド行列に回転を反映
    node.transform *= matTranslation; // ワールド行列に平行移動を反映

    // グローバル変形行列の計算
    node.globalTransform = node.transform;
    if (parent) {
        node.parent = parent;

        // 親の変形を乗算
        node.globalTransform *= parent->globalTransform;
    }

    // FBXノードの情報を解析してノードに記録(Todo)
    // FBXノードのメッシュ情報を解析(Todo)
    FbxNodeAttribute* fbxNodeAttribute = fbxNode->GetNodeAttribute();

    if (fbxNodeAttribute) {
        if (fbxNodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh) {
            fbxModel->meshNode = &node;
            ParseMesh(fbxModel, fbxNode);
        }
    }

    // 子ノードに対して再帰呼び出し
    for (int i = 0; i < fbxNode->GetChildCount(); i++) {
        ParseNodeRecursive(fbxModel, fbxNode->GetChild(i), &node);
    }
}

void FbxLoader::ParseMesh(FbxModel* fbxModel, FbxNode* fbxNode)
{
    // ノードのメッシュを取得
    FbxMesh* fbxMesh = fbxNode->GetMesh();

    // 頂点座標読み取り
    ParseMeshVertices(fbxModel, fbxMesh);
    // 面を構成するデータの読み取り
    ParseMeshFaces(fbxModel, fbxMesh);
    // マテリアルの読み取り
    ParseMaterial(fbxModel, fbxNode);
    // スキニング情報の読み取り
    ParseSkin(fbxModel, fbxMesh);
}

void FbxLoader::ConvertMatrixFromFbx(DirectX::XMMATRIX* dst, const FbxAMatrix& src)
{
    // 行
    for (int i = 0; i < 4; i++) {
        // 列
        for (int j = 0; j < 4; j++) {
            // 1要素コピー
            dst->r[i].m128_f32[j] = (float)src.Get(i, j);
        }
    }
}

void FbxLoader::ParseMeshVertices(FbxModel* fbxModel, FbxMesh* fbxMesh)
{
    auto& vertices = fbxModel->vertices;

    // 頂点座標データ数
    const int controlPointsCount = fbxMesh->GetControlPointsCount();
    // 必要数だけ頂点データ配列を確保
    FbxModel::VertexPosNormalUvSkin vert{};
    fbxModel->vertices.resize(controlPointsCount, vert);

    // FBXメッシュの頂点座標配列を取得
    FbxVector4* pCoord = fbxMesh->GetControlPoints();
    // FBXメッシュの全頂点座標をモデル内の配列にコピーする。
    for (int i = 0; i < controlPointsCount; i++) {
        FbxModel::VertexPosNormalUvSkin& vertex = vertices[i];
        // 座標のコピー
        vertex.pos.x = (float)pCoord[i][0];
        vertex.pos.y = (float)pCoord[i][1];
        vertex.pos.z = (float)pCoord[i][2];
    }
}

void FbxLoader::ParseMeshFaces(FbxModel* fbxModel, FbxMesh* fbxMesh)
{
    auto& vertices = fbxModel->vertices;
    auto& indices = fbxModel->indices;

    // 1ファイルに複数メッシュのモデルは非対応
    assert(indices.size() == 0);
    // 面の数
    const int polygonCount = fbxMesh->GetPolygonCount();
    // UVデータの数
    const int textureUVCount = fbxMesh->GetTextureUVCount();
    // UV名リスト
    FbxStringList uvNames;
    fbxMesh->GetUVSetNames(uvNames);

    // 面ごとの情報読み取り
    for (int i = 0; i < polygonCount; i++) 
    {
        // 面を構成する頂点の数を取得(3なら三角形ポリゴン)
        const int polygonSize = fbxMesh->GetPolygonSize(i);
        assert(polygonSize <= 4);

        // 1頂点ずつ処理
        for (int j = 0; j < polygonSize; j++) 
        {
            // FBX頂点配列のインデックス
            int index = fbxMesh->GetPolygonVertex(i, j);
            assert(index >= 0);

            // 頂点法線読み込み
            FbxModel::VertexPosNormalUvSkin& vertex = vertices[index];
            FbxVector4 normal;
            if (fbxMesh->GetPolygonVertexNormal(i, j, normal)) 
            {
                vertex.normal.x = (float)normal[0];
                vertex.normal.y = (float)normal[1];
                vertex.normal.z = (float)normal[2];
            }

            // テクスチャUV読み込み
            if (textureUVCount > 0)
            {
                FbxVector2 uvs;
                bool lUnmappedUV;
                // 0番決め打ちで読み込み
                if (fbxMesh->GetPolygonVertexUV(i, j, uvNames[0], uvs, lUnmappedUV)) 
                {
                    vertex.uv.x = (float)uvs[0];
                    vertex.uv.y = (float)uvs[1];
                }
            }

            // インデックス配列に頂点インデックス追加
            // 3番点目までなら
            if (j < 3){
                // 1点追加し、他の2点と三角形を構築する
                indices.push_back(index);
            }
            // 4番点目
            else {
                // 3点追加し、
                // 四角形の 0,1,2,3 の内 2,3,0 で三角形を構築する
                int index2 = indices[indices.size() - 1];
                int index3 = index;
                int index0 = indices[indices.size() - 3];
                indices.push_back(index2);
                indices.push_back(index3);
                indices.push_back(index0);
            }
        }
    }
}

void FbxLoader::ParseMaterial(FbxModel* fbxModel, FbxNode* fbxNode)
{
    const int materialCount = fbxNode->GetMaterialCount();
    if (materialCount > 0) {
        // 先頭のマテリアルを取得
        FbxSurfaceMaterial* material = fbxNode->GetMaterial(0);
        // テクスチャを読み込んだかどうかを表すフラグ
        bool textureLoaded = false;

        if (material) {
            // FbxSurfaceLambertクラスかどうかを調べる
            if (material->GetClassId().Is(FbxSurfaceLambert::ClassId)) {
                FbxSurfaceLambert* lambert = static_cast<FbxSurfaceLambert*>(material);

                // 環境光係数
                FbxPropertyT<FbxDouble3> ambient = lambert->Ambient;
                fbxModel->ambient.x = (float)ambient.Get()[0];
                fbxModel->ambient.y = (float)ambient.Get()[1];
                fbxModel->ambient.z = (float)ambient.Get()[2];

                // 拡散反射光係数
                FbxPropertyT<FbxDouble3> diffuse = lambert->Diffuse;
                fbxModel->diffuse.x = (float)diffuse.Get()[0];
                fbxModel->diffuse.y = (float)diffuse.Get()[1];
                fbxModel->diffuse.z = (float)diffuse.Get()[2];
            }

            // ディフューズテクスチャを取り出す
            const FbxProperty diffuseProperty = material->FindProperty(FbxSurfaceMaterial::sDiffuse);
            if (diffuseProperty.IsValid()) 
            {
                const FbxFileTexture* texture = diffuseProperty.GetSrcObject<FbxFileTexture>();
                if (texture) {
                    const char* filepath = texture->GetFileName();
                    // ファイルパスからファイル名抽出
                    string path_str(filepath);
                    string name = ExtractFileName(path_str);
                    // テクスチャ読み込み
                    LoadTexture(fbxModel, baseDirectory + fbxModel->name + "/" + name);
                    textureLoaded = true;
                }
            }
        }
        // テクスチャがない場合は白テクスチャを貼る
        if (!textureLoaded) {
            LoadTexture(fbxModel, baseDirectory + defaultTextureFileName);
        }
    }
}

void FbxLoader::ParseSkin(FbxModel* fbxModel, FbxMesh* fbxMesh)
{
    // スキニング情報
    FbxSkin* fbxSkin = static_cast<FbxSkin*>(fbxMesh->GetDeformer(0, FbxDeformer::eSkin));
    // スキニング情報がなければ終了
    if (fbxSkin == nullptr) {

        // 各頂点について処理
        for (int i = 0; i < fbxModel->vertices.size(); i++) {
            // 最初のボーン(単位行列)の影響100%にする
            fbxModel->vertices[i].boneIndex[0] = 0;
            fbxModel->vertices[i].boneWeight[0] = 1.0f;
        }

        return;
    }

    // ボーン配列の参照
    std::vector<FbxModel::Bone>& bones = fbxModel->bones;

    // ボーンの数
    int clusterCount = fbxSkin->GetClusterCount();
    bones.reserve(clusterCount);

    // 全てのボーンについて
    for (int i = 0; i < clusterCount; i++) {
        // FBXボーン情報
        FbxCluster* fbxCluster = fbxSkin->GetCluster(i);

        // ボーン自体のノードの名前を取得
        const char* boneName = fbxCluster->GetLink()->GetName();

        // 新しくボーンを追加し、追加したボーンの参照を得る
        bones.emplace_back(FbxModel::Bone(boneName));
        FbxModel::Bone& bone = bones.back();
        // 自作ボーンとFBXのボーンを紐づける
        bone.fbxCluster = fbxCluster;

        // FBXから初期姿勢行列を取得する
        FbxAMatrix fbxMat;
        fbxCluster->GetTransformLinkMatrix(fbxMat);

        // XMMatrix型に変換する
        XMMATRIX initialPose;
        ConvertMatrixFromFbx(&initialPose, fbxMat);

        // 初期姿勢行列の逆行列を得る
        bone.invInitialPose = XMMatrixInverse(nullptr, initialPose);
    }

    // ボーン番号とスキンウェイトのペア
    struct WeightSet {
        UINT index;
        float weight;
    };

    // 二次元配列(ジャグ配列)
    // list:頂点が影響を受けるボーンのすべてのリスト
    // vector:それを全頂点分
    std::vector<std::list<WeightSet>>weightLists(fbxModel->vertices.size());

    // すべてのボーンについて
    for (int i = 0; i < clusterCount; i++) {
        // FBXボーン情報
        FbxCluster* fbxCluster = fbxSkin->GetCluster(i);
        // このボーンに影響を受ける頂点の数
        int countrolPointIndicesCount = fbxCluster->GetControlPointIndicesCount();
        // このボーンに影響を受ける頂点の配列
        int* controlPointIndices = fbxCluster->GetControlPointIndices();
        double* controlPointWeights = fbxCluster->GetControlPointWeights();

        // 影響を受ける全頂点について
        for (int j = 0; j < countrolPointIndicesCount; j++) {
            // 頂点番号
            int vertIndex = controlPointIndices[j];
            // スキンウェイト
            float weight = (float)controlPointWeights[j];
            // その頂点の影響を受けるボーンリストに、ボーンとウェイトのペアを追加
            weightLists[vertIndex].emplace_back(WeightSet{ (UINT)i, weight });
        }
    }

    // 頂点配列書き換え用の参照
    auto& vertices = fbxModel->vertices;
    // 各頂点について処理
    for (int i = 0; i < vertices.size(); i++) {
        // 頂点のウェイトから最も大きい4つを選択
        auto& weightList = weightLists[i];
        // 大小比較用のラムダ式を指定して降順にソート
        weightList.sort(
            [](auto const& lhs, auto const& rhs) {
                // 左の要素のほうが大きければtrue それでなければfalseを返す
                return lhs.weight > rhs.weight;
            });

        int weightArrayIndex = 0;
        // 降順ソート済みのウェイトリストから
        for (auto& weightSet : weightList) {
            // 頂点データに書き込み
            vertices[i].boneIndex[weightArrayIndex] = weightSet.index;
            vertices[i].boneWeight[weightArrayIndex] = weightSet.weight;
            // 4つに達したら終了
            if (++weightArrayIndex >= FbxModel::MAX_BONE_INDICES) {
                float weight = 0.0f;
                // 2番目以降のウェイトを合計
                for (int j = 1; j < FbxModel::MAX_BONE_INDICES; j++) {
                    weight += vertices[i].boneWeight[j];
                }
                // 合計で1.0f(100%)になるように調整
                vertices[i].boneWeight[0] = 1.0f - weight;
                break;
            }
        }
    }
}

void FbxLoader::LoadTexture(FbxModel* fbxModel, const std::string& fullpath)
{
    HRESULT result = S_FALSE;
    // WICテクスチャのロード
    TexMetadata& metadata = fbxModel->metadata;
    ScratchImage& scratchImg = fbxModel->scratchImg;
    // ユニコード文字列に変換
    wchar_t wfilepath[128];
    MultiByteToWideChar(CP_ACP, 0, fullpath.c_str(), -1, wfilepath, _countof(wfilepath));
    result = LoadFromWICFile(
        wfilepath, WIC_FLAGS_NONE,
        &metadata, scratchImg);
    if (FAILED(result)) {
        assert(0);
    }
}

std::string FbxLoader::ExtractFileName(const std::string& path)
{
    size_t pos1;
    // 区切り文字 '\\' が出てくる一番最後の部分を検索
    pos1 = path.rfind('\\');
    if (pos1 != string::npos) {
        return path.substr(pos1 + 1, path.size() - pos1 - 1);
    }
    // 区切り文字 '/' が出てくる一番最後の部分を検索
    pos1 = path.rfind('/');
    if (pos1 != string::npos) {
        return path.substr(pos1 + 1, path.size() - pos1 - 1);
    }

    return path;
}
