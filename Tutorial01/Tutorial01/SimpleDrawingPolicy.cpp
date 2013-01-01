#include "SimpleDrawingPolicy.h"
#include "Engine.h"
#include "StaticMesh.h"
#include <cassert>

struct ConstantBufferStruct
{
	XMMATRIX mWorld;
	XMMATRIX mView;
	XMMATRIX mProjection;
	XMFLOAT4 vLightDir[2];
	XMFLOAT4 vLightColor[2];
};

SimpleDrawingPolicy::SimpleDrawingPolicy(void)
	:ConstantBuffer(NULL)
{
	FileName = "SimpleShader.fx";

	HRESULT hr;
	// Create the constant buffer
	D3D11_BUFFER_DESC bdc;
	ZeroMemory( &bdc, sizeof(bdc) );
	bdc.Usage = D3D11_USAGE_DEFAULT;
	bdc.ByteWidth = sizeof(ConstantBufferStruct);
	bdc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bdc.CPUAccessFlags = 0;
	hr = GEngine->Device->CreateBuffer( &bdc, NULL, &ConstantBuffer );
	if( FAILED( hr ) )
		assert(false);
}


SimpleDrawingPolicy::~SimpleDrawingPolicy(void)
{
	ConstantBuffer->Release();
}

void SimpleDrawingPolicy::DrawStaticMesh( StaticMesh* pMesh )
{
	XMMATRIX World;

	World = XMMatrixIdentity();
	ConstantBufferStruct cb;
	cb.mWorld = XMMatrixTranspose( World );
	cb.mView = XMMatrixTranspose( GEngine->ViewMat );
	cb.mProjection = XMMatrixTranspose( GEngine->ProjectionMat );
	cb.vLightDir[0] = vLightDirs[0];
	cb.vLightDir[1] = vLightDirs[1];
	cb.vLightColor[0] = vLightColors[0];
	cb.vLightColor[1] = vLightColors[1];
	GEngine->ImmediateContext->UpdateSubresource( ConstantBuffer, 0, NULL, &cb, 0, 0 );

	ShaderRes* pShaderRes = GetShaderRes(pMesh->_NumTexCoord);


	pShaderRes->SetShaderRes();

	UINT offset = 0;
	GEngine->ImmediateContext->IASetVertexBuffers( 0, 1, &pMesh->_VertexBuffer, &pMesh->_VertexStride, &offset );
	GEngine->ImmediateContext->IASetIndexBuffer( pMesh->_IndexBuffer, DXGI_FORMAT_R16_UINT, 0 );

	GEngine->ImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );


	GEngine->ImmediateContext->VSSetConstantBuffers( 0, 1, &ConstantBuffer );
	GEngine->ImmediateContext->PSSetConstantBuffers( 0, 1, &ConstantBuffer );

	//GEngine->ImmediateContext->PSSetShaderResources( 0, 1, &g_pTextureRV );
	//GEngine->ImmediateContext->PSSetSamplers( 0, 1, &g_pSamplerLinear );
	GEngine->ImmediateContext->DrawIndexed( pMesh->_NumTriangle*3, 0, 0 );        // 36 vertices needed for 12 triangles in a triangle list
}


