#include "GBufferDrawingPolicy.h"
#include "StateManager.h"


struct ConstantBufferStruct
{
	XMMATRIX mModelView;
	XMMATRIX mProjection;
};

GBufferDrawingPolicy::GBufferDrawingPolicy(void)
	:ConstantBuffer(NULL)
	,_VertexShader(NULL)
{
	FileName = "GBufferShader.fx";

	HRESULT hr;
	// Create the constant buffer
	D3D11_BUFFER_DESC bdc;
	ZeroMemory( &bdc, sizeof(bdc) );
	bdc.Usage = D3D11_USAGE_DEFAULT;
	bdc.ByteWidth = sizeof(ConstantBufferStruct);
	bdc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bdc.CPUAccessFlags = 0;
	hr = GEngine->_Device->CreateBuffer( &bdc, NULL, &ConstantBuffer );
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName("GBufferDrawingPolicyConstantBuffer", ConstantBuffer);

	_VertexShader = new GBufferVertexShader("GBufferShader.fx", "VS");

}


GBufferDrawingPolicy::~GBufferDrawingPolicy(void)
{
	if(ConstantBuffer) ConstantBuffer->Release();
	if(_VertexShader) delete _VertexShader;
}

void GBufferDrawingPolicy::DrawStaticMesh( StaticMesh* pMesh, XMMATRIX& ViewMat, XMMATRIX& ProjectionMat )
{
	XMMATRIX World;

	ConstantBufferStruct cb;
	cb.mModelView = XMMatrixTranspose( ViewMat );
	cb.mProjection = XMMatrixTranspose( ProjectionMat);

	GEngine->_ImmediateContext->UpdateSubresource( ConstantBuffer, 0, NULL, &cb, 0, 0 );

	ShaderRes* pShaderRes = GetShaderRes(pMesh->_NumTexCoord, StaticVertex);


	pShaderRes->SetShaderRes();

	_VertexShader->SetShader(Static, pMesh->_NumTexCoord);

	UINT offset = 0;
	GEngine->_ImmediateContext->IASetVertexBuffers( 0, 1, &pMesh->_VertexBuffer, &pMesh->_VertexStride, &offset );
	GEngine->_ImmediateContext->IASetIndexBuffer( pMesh->_IndexBuffer, DXGI_FORMAT_R32_UINT, 0 );

	GEngine->_ImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );


	GEngine->_ImmediateContext->VSSetConstantBuffers( 0, 1, &ConstantBuffer );
	GEngine->_ImmediateContext->PSSetConstantBuffers( 0, 1, &ConstantBuffer );

	SET_PS_SAMPLER(0, SS_LINEAR);


	GEngine->_ImmediateContext->DrawIndexed( pMesh->_NumTriangle*3, 0, 0 );        // 36 vertices needed for 12 triangles in a triangle list
}


void GBufferDrawingPolicy::DrawSkeletalMeshData( SkeletalMeshRenderData* pRenderData, XMMATRIX& ViewMat, XMMATRIX& ProjectionMat )
{
	XMMATRIX World;

	ConstantBufferStruct cb;
	
	cb.mModelView = XMMatrixTranspose( ViewMat );
	cb.mProjection = XMMatrixTranspose( ProjectionMat);

	GEngine->_ImmediateContext->UpdateSubresource( ConstantBuffer, 0, NULL, &cb, 0, 0 );

	ShaderRes* pShaderRes = GetShaderRes(pRenderData->_SkeletalMesh->_NumTexCoord, GpuSkinVertex);


	pShaderRes->SetShaderRes();

	UINT offset = 0;
	GEngine->_ImmediateContext->IASetVertexBuffers( 0, 1, &pRenderData->_SkeletalMesh->_VertexBuffer, &pRenderData->_SkeletalMesh->_VertexStride, &offset );
	GEngine->_ImmediateContext->IASetIndexBuffer( pRenderData->_SkeletalMesh->_IndexBuffer, DXGI_FORMAT_R32_UINT, 0 );

	GEngine->_ImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );


	GEngine->_ImmediateContext->VSSetConstantBuffers( 0, 1, &ConstantBuffer );
	GEngine->_ImmediateContext->PSSetConstantBuffers( 0, 1, &ConstantBuffer );

	GEngine->_ImmediateContext->VSSetShaderResources( 0, 1, &pRenderData->_BoneMatricesBufferRV );

	SET_PS_SAMPLER(0, SS_LINEAR);

	GEngine->_ImmediateContext->DrawIndexed( pRenderData->_SkeletalMesh->_NumTriangle*3, 0, 0 );        // 36 vertices needed for 12 triangles in a triangle list
}