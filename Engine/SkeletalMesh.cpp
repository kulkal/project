#include <cassert>
#include <algorithm>

#include "OutputDebug.h"
#include "SkeletalMesh.h"
#include "Engine.h"
#include "LineBatcher.h"

const int TRIANGLE_VERTEX_COUNT = 3;
const int VERTEX_STRIDE = 4;
const int NORMAL_STRIDE = 3;
const int UV_STRIDE = 2;

SkeletalMesh::SkeletalMesh(void)
	:_VertexBuffer(NULL),
	_IndexBuffer(NULL),
	_BoneMatricesBuffer(NULL),
	_BoneMatricesBufferRV(NULL),
	_VertexStride(0),
	_NumTexCoord(0),
	_NumTriangle(0),
	_NumVertex(0),
	_Skeleton(NULL),
	_Pose(NULL)
{
}


SkeletalMesh::~SkeletalMesh(void)
{
	if(_VertexBuffer) _VertexBuffer->Release();
	if(_IndexBuffer) _IndexBuffer->Release();
	if(_BoneMatricesBuffer) _BoneMatricesBuffer->Release();
	if(_BoneMatricesBufferRV) _BoneMatricesBufferRV->Release();
	
	if(_Skeleton) delete _Skeleton;
	if(_Pose) delete _Pose;

	for(unsigned int i=0;i<_SubMeshArray.size();i++)
	{
		delete _SubMeshArray[i];
	}
}

#define MAXBONE_VERTEX 4
struct BoneInf
{
	std::string BoneName;
	float Weight;
	bool operator<(const BoneInf& other) const        
	{             
		if( Weight > other.Weight )
			return true;
		else
			return false;
	};
};
struct VertexSkinInfo
{
	std::vector<BoneInf> BoneLink;

};

bool SkeletalMesh::ImportFromFbxMesh( FbxMesh* Mesh, FbxFileImporter* Importer )
{
	if (!Mesh->GetNode())
		return false;

	FbxNode* pNode = Mesh->GetNode();
	FbxAMatrix Geometry;
	FbxVector4 Translation, Rotation, Scaling;
	Translation = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
	Rotation = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
	Scaling = pNode->GetGeometricScaling(FbxNode::eSourcePivot);
	Geometry.SetT(Translation);
	Geometry.SetR(Rotation);
	Geometry.SetS(Scaling);

	//For Single Matrix situation, obtain transfrom matrix from eDESTINATION_SET, which include pivot offsets and pre/post rotations.
	FbxAMatrix& GlobalTransform = Importer->mScene->GetEvaluator()->GetNodeGlobalTransform(pNode);

	FbxAMatrix TotalMatrix;
	TotalMatrix = GlobalTransform * Geometry;

	FbxAMatrix TotalMatrixForNormal;
	TotalMatrixForNormal = TotalMatrix.Inverse();
	TotalMatrixForNormal = TotalMatrixForNormal.Transpose();
	
	if (!Mesh->IsTriangleMesh())
	{
		FbxGeometryConverter lConverter(Mesh->GetNode()->GetFbxManager());
		bool bSuccess;
		Mesh = lConverter.TriangulateMeshAdvance(Mesh, bSuccess);
	}
	
	const int PolygonCount = Mesh->GetPolygonCount();

	FbxLayerElementArrayTemplate<int>* MaterialIndice = NULL;
	FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eNone;


	FbxGeometryElementMaterial * ElementMaterial = Mesh->GetElementMaterial();
	if (ElementMaterial)
	{
		MaterialIndice = &ElementMaterial->GetIndexArray();
		MaterialMappingMode = ElementMaterial->GetMappingMode();
		if (MaterialIndice && MaterialMappingMode == FbxGeometryElement::eByPolygon)
		{
			FBX_ASSERT(MaterialIndice->GetCount() == PolygonCount);
			if (MaterialIndice->GetCount() == PolygonCount)
			{
				// Count the faces of each material
				for (int PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
				{
					const int lMaterialIndex = MaterialIndice->GetAt(PolygonIndex);
					if ((int)_SubMeshArray.size() < lMaterialIndex + 1)
					{
						_SubMeshArray.resize(lMaterialIndex + 1);
					}
					if (_SubMeshArray[lMaterialIndex] == NULL)
					{
						_SubMeshArray[lMaterialIndex] = new SubMesh;
					}
					_SubMeshArray[lMaterialIndex]->_TriangleCount += 1;
				}

				// Make sure we have no "holes" (NULL) in the mSubMeshes table. This can happen
				// if, in the loop above, we resized the mSubMeshes by more than one slot.
				for (int i = 0; i < (int)_SubMeshArray.size(); i++)
				{
					if (_SubMeshArray[i] == NULL)
						_SubMeshArray[i] = new SubMesh;

				}

				// Record the offset (how many vertex)
				const int lMaterialCount = _SubMeshArray.size();
				int lOffset = 0;
				for (int lIndex = 0; lIndex < lMaterialCount; ++lIndex)
				{
					_SubMeshArray[lIndex]->_IndexOffset = lOffset;
					lOffset += _SubMeshArray[lIndex]->_TriangleCount * 3;
					// This will be used as counter in the following procedures, reset to zero
					_SubMeshArray[lIndex]->_TriangleCount = 0;
				}
				FBX_ASSERT(lOffset == PolygonCount * 3);
			}
		}

		
	}

	// All faces will use the same material.
	if (_SubMeshArray.size() == 0)
	{
		_SubMeshArray.resize(1);
		_SubMeshArray[0] = new SubMesh();
	}

	// Congregate all the data of a mesh to be cached in VBOs.
	// If normal or UV is by polygon vertex, record all vertex attributes by polygon vertex.
	bool mHasNormal = Mesh->GetElementNormalCount() > 0;
	bool mHasUV = Mesh->GetElementUVCount() > 0;
	bool mAllByControlPoint = true;
	FbxGeometryElement::EMappingMode lNormalMappingMode = FbxGeometryElement::eNone;
	FbxGeometryElement::EMappingMode lUVMappingMode = FbxGeometryElement::eNone;
	if (mHasNormal)
	{
		lNormalMappingMode = Mesh->GetElementNormal(0)->GetMappingMode();
		if (lNormalMappingMode == FbxGeometryElement::eNone)
		{
			mHasNormal = false;
		}
		if (mHasNormal && lNormalMappingMode != FbxGeometryElement::eByControlPoint)
		{
			//mAllByControlPoint = false;
		}
	}
	if (mHasUV)
	{
		lUVMappingMode = Mesh->GetElementUV(0)->GetMappingMode();
		if (lUVMappingMode == FbxGeometryElement::eNone)
		{
			mHasUV = false;
		}
		if (mHasUV && lUVMappingMode != FbxGeometryElement::eByControlPoint)
		{
			//mAllByControlPoint = false;
		}
	}


	// Allocate the array memory, by control point or by polygon vertex.
	int lPolygonVertexCount = Mesh->GetControlPointsCount();
	if (!mAllByControlPoint)
	{
		lPolygonVertexCount = PolygonCount * TRIANGLE_VERTEX_COUNT;
	}
	//_PositionArray = new XMFLOAT3[lPolygonVertexCount];
	_PositionArray.resize(lPolygonVertexCount);
	//_IndiceArray = new WORD[PolygonCount * TRIANGLE_VERTEX_COUNT];
	_IndiceArray.resize(PolygonCount * TRIANGLE_VERTEX_COUNT);

	if (mHasNormal)
	{
		//_NormalArray = new XMFLOAT3[lPolygonVertexCount];
		_NormalArray.resize(lPolygonVertexCount);
	}
	FbxStringList lUVNames;
	Mesh->GetUVSetNames(lUVNames);
	const char * lUVName = NULL;
	if (mHasUV && lUVNames.GetCount())
	{
		//_TexCoordArray = new XMFLOAT2[lPolygonVertexCount];
		_TexCoordArray.resize(lPolygonVertexCount);
		lUVName = lUVNames[0];
	}

	// Populate the array with vertex attribute, if by control point.
	const FbxVector4 * lControlPoints = Mesh->GetControlPoints();
	FbxVector4 lCurrentVertex;
	FbxVector4 lCurrentNormal;
	FbxVector2 lCurrentUV;
	if (mAllByControlPoint)
	{
		const FbxGeometryElementNormal * lNormalElement = NULL;
		const FbxGeometryElementUV * lUVElement = NULL;
		if (mHasNormal)
		{
			lNormalElement = Mesh->GetElementNormal(0);
		}
		if (mHasUV)
		{
			lUVElement = Mesh->GetElementUV(0);
		}
		for (int lIndex = 0; lIndex < lPolygonVertexCount; ++lIndex)
		{
			// Save the vertex position.
			lCurrentVertex = lControlPoints[lIndex];
			FbxVector4 FinalPosition = lCurrentVertex;//TotalMatrix.MultT(lCurrentVertex);


			_PositionArray[lIndex].x = static_cast<float>(FinalPosition[0]);
			_PositionArray[lIndex].y = static_cast<float>(FinalPosition[1]);
			_PositionArray[lIndex].z = static_cast<float>(FinalPosition[2]);

			// Save the normal.
			if (mHasNormal)
			{
				int lNormalIndex = lIndex;
				if (lNormalElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lNormalIndex = lNormalElement->GetIndexArray().GetAt(lIndex);
				}
				lCurrentNormal = lNormalElement->GetDirectArray().GetAt(lNormalIndex);

				FbxVector4 FinalNormal = lCurrentNormal;//TotalMatrixForNormal.MultT(lCurrentNormal);
				//FbxVector4 FinalNormal = TotalMatrixForNormal.MultT(lCurrentNormal);
				FinalNormal.Normalize();
				_NormalArray[lIndex].x = static_cast<float>(FinalNormal[0]);
				_NormalArray[lIndex].y = static_cast<float>(FinalNormal[1]);
				_NormalArray[lIndex].z = static_cast<float>(lCurrentNormal[2]);
			}

			// Save the UV.
			if (mHasUV)
			{
				int lUVIndex = lIndex;
				if (lUVElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lUVIndex = lUVElement->GetIndexArray().GetAt(lIndex);
				}
				lCurrentUV = lUVElement->GetDirectArray().GetAt(lUVIndex);
				_TexCoordArray[lIndex].x = static_cast<float>(lCurrentUV[0]);
				_TexCoordArray[lIndex].y = static_cast<float>(lCurrentUV[1]);
			}
		}

	}

	int lVertexCount = 0;
	for (int lPolygonIndex = 0; lPolygonIndex < PolygonCount; ++lPolygonIndex)
	{
		// The material for current face.
		int lMaterialIndex = 0;
		if (MaterialIndice && MaterialMappingMode == FbxGeometryElement::eByPolygon)
		{
			lMaterialIndex = MaterialIndice->GetAt(lPolygonIndex);
		}

		// Where should I save the vertex attribute index, according to the material
		const int lIndexOffset = _SubMeshArray[lMaterialIndex]->_IndexOffset +
			_SubMeshArray[lMaterialIndex]->_TriangleCount * 3;
		for (int lVerticeIndex = 0; lVerticeIndex < TRIANGLE_VERTEX_COUNT; ++lVerticeIndex)
		{
			const int lControlPointIndex = Mesh->GetPolygonVertex(lPolygonIndex, lVerticeIndex);

			if (mAllByControlPoint)
			{
				_IndiceArray[lIndexOffset + lVerticeIndex] = static_cast<DWORD>(lControlPointIndex);

			}
			// Populate the array with vertex attribute, if by polygon vertex.
			else
			{
				_IndiceArray[lIndexOffset + lVerticeIndex] = static_cast<DWORD>(lVertexCount);


				lCurrentVertex = lControlPoints[lControlPointIndex];
				FbxVector4 FinalPosition = lCurrentVertex;//TotalMatrix.MultT(lCurrentVertex);

				_PositionArray[lVertexCount].x =  static_cast<float>(FinalPosition[0]);
				_PositionArray[lVertexCount].y =  static_cast<float>(FinalPosition[1]);
				_PositionArray[lVertexCount].z =  static_cast<float>(FinalPosition[2]);



				if (mHasNormal)
				{
					Mesh->GetPolygonVertexNormal(lPolygonIndex, lVerticeIndex, lCurrentNormal);
					FbxVector4 FinalNormal =lCurrentNormal;// TotalMatrixForNormal.MultT(lCurrentNormal);
					FinalNormal.Normalize();
					_NormalArray[lVertexCount].x = static_cast<float>(FinalNormal[0]);
					_NormalArray[lVertexCount].y = static_cast<float>(FinalNormal[1]);
					_NormalArray[lVertexCount].z = static_cast<float>(FinalNormal[2]);

				}

				if (mHasUV)
				{
					Mesh->GetPolygonVertexUV(lPolygonIndex, lVerticeIndex, lUVName, lCurrentUV);

					_TexCoordArray[lVertexCount].x = static_cast<float>(lCurrentUV[0]);
					_TexCoordArray[lVertexCount].y = static_cast<float>(lCurrentUV[1]);
				}
			}
			++lVertexCount;
		}
		_SubMeshArray[lMaterialIndex]->_TriangleCount += 1;
	}

	// If it has some defomer connection, update the vertices position
	const bool lHasVertexCache = Mesh->GetDeformerCount(FbxDeformer::eVertexCache) &&
		(static_cast<FbxVertexCacheDeformer*>(Mesh->GetDeformer(0, FbxDeformer::eVertexCache)))->IsActive();
	const bool lHasShape = Mesh->GetShapeCount() > 0;
	const bool lHasSkin = Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
	const bool lHasDeformation = lHasVertexCache || lHasShape || lHasSkin;
	
	const int lVertexCount1 = Mesh->GetControlPointsCount();
	FbxPose * lPose = NULL;
	lPose = Importer->mScene->GetPose(0);
	FbxAMatrix pGlobalPosition;
	FbxTimeSpan lTimeLineTimeSpan;
	Importer->mScene->GetGlobalSettings().GetTimelineDefaultTimeSpan(lTimeLineTimeSpan);
	FbxVector4* lVertexArray = NULL;
	if (lHasDeformation)
	{
		lVertexArray = new FbxVector4[lVertexCount1];
		memcpy(lVertexArray, Mesh->GetControlPoints(), lVertexCount1 * sizeof(FbxVector4));
	}
	if (lHasDeformation)
	{
		// Active vertex cache deformer will overwrite any other deformer
		if (lHasVertexCache)
		{
		}
		else
		{
			//we need to get the number of clusters
			const int lSkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
			int lClusterCount = 0;
			for (int lSkinIndex = 0; lSkinIndex < lSkinCount; ++lSkinIndex)
			{
				lClusterCount += ((FbxSkin *)(Mesh->GetDeformer(lSkinIndex, FbxDeformer::eSkin)))->GetClusterCount();
			}
			if (lClusterCount)
			{
				// Deform the vertex array with the skin deformer.
				FbxSkin * lSkinDeformer = (FbxSkin *)Mesh->GetDeformer(0, FbxDeformer::eSkin);
				FbxSkin::EType lSkinningType = lSkinDeformer->GetSkinningType();

				if(lSkinningType == FbxSkin::eLinear || lSkinningType == FbxSkin::eRigid)
				{
					//ComputeLinearDeformation(pGlobalPosition, pMesh, pTime, pVertexArray, pPose);
					// All the links must have the same link mode.
					FbxCluster::ELinkMode lClusterMode = ((FbxSkin*)Mesh->GetDeformer(0, FbxDeformer::eSkin))->GetCluster(0)->GetLinkMode();

					int lVertexCount = Mesh->GetControlPointsCount();
					VertexSkinInfo* SkinInfoArray = new VertexSkinInfo[lVertexCount];

					if (lClusterMode == FbxCluster::eAdditive)
					{
						for (int i = 0; i < lVertexCount; ++i)
						{
							//lClusterDeformation[i].SetIdentity();
						}
					}

					int lSkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
					for ( int lSkinIndex=0; lSkinIndex<lSkinCount; ++lSkinIndex)
					{
						FbxSkin * lSkinDeformer = (FbxSkin *)Mesh->GetDeformer(lSkinIndex, FbxDeformer::eSkin);

						int lClusterCount = lSkinDeformer->GetClusterCount();
						for ( int lClusterIndex=0; lClusterIndex<lClusterCount; ++lClusterIndex)
						{
							FbxCluster* lCluster = lSkinDeformer->GetCluster(lClusterIndex);
							if (!lCluster->GetLink())
								continue;

							FbxNode* Bone = lCluster->GetLink();


							FbxAMatrix lVertexTransformMatrix;
							//ComputeClusterDeformation(pGlobalPosition, pMesh, lCluster, lVertexTransformMatrix, pTime, pPose);

							int lVertexIndexCount = lCluster->GetControlPointIndicesCount();
							for (int k = 0; k < lVertexIndexCount; ++k) 
							{            
								int lIndex = lCluster->GetControlPointIndices()[k];
								
								double lWeight = lCluster->GetControlPointWeights()[k];
								
								BoneInf Inf;
								Inf.BoneName = Bone->GetName();
								Inf.Weight = (float)lWeight;
								SkinInfoArray[lIndex].BoneLink.push_back(Inf);
								// Sometimes, the mesh can have less points than at the time of the skinning
								// because a smooth operator was active when skinning but has been deactivated during export.
								if (lIndex >= lVertexCount)
									continue;


								if (lWeight == 0.0)
								{
									continue;
								}
							
							}//For each vertex			
						}//lClusterCount
					}

					int numOverLink = 0;
					for(int i=0;i<lVertexCount;i++)
					{
						VertexSkinInfo& SkinInfo = SkinInfoArray[i];
						if(SkinInfo.BoneLink.size() > 4)
						{
							numOverLink++;
							std::sort (SkinInfo.BoneLink.begin(), SkinInfo.BoneLink.end());
							for(unsigned int OverIndex=4;OverIndex<SkinInfo.BoneLink.size();OverIndex++)
							{
								for(int k=0;k<4;k++)
								{
									SkinInfo.BoneLink[k].Weight += SkinInfo.BoneLink[OverIndex].Weight/4.f;
								}
							}
							int NumErase = SkinInfo.BoneLink.size() - 4;
							SkinInfo.BoneLink.erase(SkinInfo.BoneLink.end()-NumErase, SkinInfo.BoneLink.end());
						}
						float WeightTotal = 0.f;
						for(unsigned int k=0;k<SkinInfo.BoneLink.size();k++)
						{
							WeightTotal += SkinInfo.BoneLink[k].Weight;
						}

						assert(WeightTotal >= 0.999f);
					}

					std::map<std::string, BoneIndexInfo>& BoneIndexMap = Importer->BoneIndexMap;
					for(int i=0;i<lVertexCount;i++)
					{
						VertexSkinInfo& SkinInfo = SkinInfoArray[i];
						for(unsigned int k=0;k<SkinInfo.BoneLink.size();k++)
						{
							std::string& LinkedBoneName = SkinInfo.BoneLink[k].BoneName;
							std::map<std::string, BoneIndexInfo>::iterator it;
							it = BoneIndexMap.find(LinkedBoneName);
							if(it != BoneIndexMap.end())
							{
								BoneIndexInfo& LinkInfo = it->second;
								LinkInfo.IsUsedLink = true;
							}
						}
					}

					std::map<std::string, BoneIndexInfo>::iterator it;
					for(it=BoneIndexMap.begin();it!=BoneIndexMap.end();)
					{
						BoneIndexInfo& LinkInfo = it->second;
						if(LinkInfo.IsUsedLink == false)
						{
							it = BoneIndexMap.erase(it);
						}
						else
							it++;
					}

					std::vector<BoneIndexInfo>& BoneLinkArray = Importer->BoneArray;

					for(it=BoneIndexMap.begin();it!=BoneIndexMap.end();it++)
					{
						BoneIndexInfo& LinkInfo = it->second;
						BoneLinkArray.push_back(LinkInfo);
					}

					std::sort(BoneLinkArray.begin(),BoneLinkArray.end());

					for(unsigned int BoneIndex=0;BoneIndex<BoneLinkArray.size();BoneIndex++)
					{
						BoneIndexInfo& LinkInfo = BoneLinkArray[BoneIndex];

						std::map<std::string, BoneIndexInfo>::iterator it;
						it = BoneIndexMap.find(LinkInfo.BoneName);
						if(it != BoneIndexMap.end())
						{
							BoneIndexInfo& LinkInfoMap = it->second;
							LinkInfoMap.Index = BoneIndex;
						}
					}

					//_SkinInfoArray = new SkinInfo[lVertexCount];
					_SkinInfoArray.resize(lVertexCount);
					for(int Vert=0;Vert<lVertexCount;Vert++)
					{
						VertexSkinInfo& SkinInfo = SkinInfoArray[Vert];
						for(unsigned int BIdx=0;BIdx<SkinInfo.BoneLink.size();BIdx++)
						{
							_SkinInfoArray[Vert].Weights[BIdx] = SkinInfo.BoneLink[BIdx].Weight;

							// find real bone index
							std::map<std::string, BoneIndexInfo>::iterator it;
							it = BoneIndexMap.find(SkinInfo.BoneLink[BIdx].BoneName);
							if(it != BoneIndexMap.end())
							{
								BoneIndexInfo& LinkInfoMap = it->second;
								_SkinInfoArray[Vert].Bones[BIdx] = LinkInfoMap.Index;
							}
						}
						int Remain = 4-SkinInfo.BoneLink.size();
						for(int r=0;r<Remain;r++)
						{
							int RIndex = 3-r;
							_SkinInfoArray[Vert].Weights[RIndex] = 0.f;
							_SkinInfoArray[Vert].Bones[RIndex] = 0;

						
						}
					}

					// fill ref pose matrices
					_NumBone = BoneLinkArray.size();

					
					_RequiredBoneArray.resize(_NumBone);
					for(int i=0;i<_NumBone;i++)
					{
						_RequiredBoneArray[i] = BoneLinkArray[i].SkeletonIndex;
					}
					delete[] SkinInfoArray;
				}
			}
		}
	}

	delete [] lVertexArray;

	//
	HRESULT hr;
	D3D11_BUFFER_DESC bd;
	ZeroMemory( &bd, sizeof(bd) );
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory( &InitData, sizeof(InitData) );
	if(mHasNormal == true && mHasUV == false)
	{
		bd.ByteWidth = sizeof( NormalVertexGpuSkin ) * lPolygonVertexCount;
		NormalVertexGpuSkin* Vertices = new NormalVertexGpuSkin[lPolygonVertexCount];
		for(int i = 0;i<lPolygonVertexCount;i++)
		{
			Vertices[i].Position = _PositionArray[i];
			Vertices[i].Normal = _NormalArray[i];
			
			Vertices[i].Weights = 0x00000000;
			Vertices[i].Bones = 0x00000000;

			for(int k=0;k<MAX_BONELINK;k++)
			{
				Vertices[i].Weights |=  (unsigned int)(_SkinInfoArray[i].Weights[k] * 255.f) << k*8;
			}
			for(int k=0;k<MAX_BONELINK;k++)
			{
				Vertices[i].Bones |= (unsigned int)_SkinInfoArray[i].Bones[k] << k*8;
			}
		}
		InitData.pSysMem = Vertices;
		hr = GEngine->_Device->CreateBuffer( &bd, &InitData, &_VertexBuffer );
		if( FAILED( hr ) )
		{
			assert(false);
			return false;
		}

		SetD3DResourceDebugName("SkeletalMesh_VertexBuffer", _VertexBuffer);


		delete[] Vertices;
	}
	else if(mHasNormal == true && mHasUV == true)
	{
		bd.ByteWidth = sizeof( NormalTexVertexGpuSkin ) * lPolygonVertexCount;
		NormalTexVertexGpuSkin* Vertices = new NormalTexVertexGpuSkin[lPolygonVertexCount];
		for(int i = 0;i<lPolygonVertexCount;i++)
		{
			Vertices[i].Position = _PositionArray[i];
			Vertices[i].Normal = _NormalArray[i];
			Vertices[i].TexCoord = _TexCoordArray[i];
			
			Vertices[i].Weights = 0x00000000;
			Vertices[i].Bones = 0x00000000;
			
			for(int k=0;k<MAX_BONELINK;k++)
			{
				Vertices[i].Weights |=  (unsigned int)(_SkinInfoArray[i].Weights[k] * 255.f) << k*8;
			}
			for(int k=0;k<MAX_BONELINK;k++)
			{
				Vertices[i].Bones |= (unsigned int)_SkinInfoArray[i].Bones[k] << k*8;
				
			}
		}
		InitData.pSysMem = Vertices;
		hr = GEngine->_Device->CreateBuffer( &bd, &InitData, &_VertexBuffer );
		if( FAILED( hr ) )
		{
			assert(false);
			return false;
		}
		SetD3DResourceDebugName("SkeletalMesh_VertexBuffer", _VertexBuffer);

		delete[] Vertices;
	}
	

	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof( DWORD ) * PolygonCount * TRIANGLE_VERTEX_COUNT;        // 36 vertices needed for 12 triangles in a triangle list
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;
	InitData.pSysMem = &_IndiceArray[0];
	hr = GEngine->_Device->CreateBuffer( &bd, &InitData, &_IndexBuffer );
	if( FAILED( hr ) )
	{
		assert(false);
		return false;
	}

	SetD3DResourceDebugName("SkeletalMesh_IndexBuffer", _IndexBuffer);


	_NumTriangle = PolygonCount;
	_NumVertex = lPolygonVertexCount;

	if(_NormalArray.size() != 0 && _TexCoordArray.size() == 0)
	{
		_VertexStride = sizeof(NormalVertexGpuSkin);
		_NumTexCoord = 0;
	}
	if(_NormalArray.size() != 0L && _TexCoordArray.size() != 0)

	{
		_VertexStride = sizeof(NormalTexVertexGpuSkin);
		_NumTexCoord = 1;
	}

	return true;
}
