#pragma once
#include "drawingpolicy.h"
class GBufferDrawingPolicy :
	public DrawingPolicy
{
	ID3D11Buffer*           ConstantBuffer;
public:
	virtual void DrawStaticMesh(StaticMesh* pMesh, XMMATRIX& ViewMat, XMMATRIX& ProjectionMat);
	virtual void DrawSkeletalMeshData(SkeletalMeshRenderData* pRenderData, XMMATRIX& ViewMat, XMMATRIX& ProjectionMat);
	
	GBufferDrawingPolicy(void);
	virtual ~GBufferDrawingPolicy(void);
};

