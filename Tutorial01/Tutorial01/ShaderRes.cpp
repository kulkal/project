#include <cassert>
#include <vector>
#include "ShaderRes.h"
#include "Engine.h"

ShaderRes::ShaderRes(void)
	:VertexLayout(NULL),
	VertexShader(NULL),
	PixelShader(NULL)
{
}


ShaderRes::~ShaderRes(void)
{
	if(VertexLayout) VertexLayout->Release();
	if(VertexShader) VertexShader->Release();
	if(PixelShader) PixelShader->Release();
}

void ShaderRes::CreateShader(const char* FileName, ShaderMapKey& SKey)
{
	std::vector<D3D10_SHADER_MACRO> Defines;
	if(SKey.NumTex == 0)
	{
		D3D10_SHADER_MACRO Define = {"TEXCOORD", "0"};
		Defines.push_back(Define);
	}
	else if(SKey.NumTex == 1)
	{
		D3D10_SHADER_MACRO Define = {"TEXCOORD", "1"};
		Defines.push_back(Define);
	}
	D3D10_SHADER_MACRO Define;
	memset(&Define, 0, sizeof(D3D10_SHADER_MACRO));
	Defines.push_back(Define);
	
	int nLen = strlen(FileName)+1;

	wchar_t WFileName[1024];
	mbstowcs(WFileName, FileName, nLen);

	HRESULT hr;
	ID3DBlob* pVSBlob = NULL;
	hr = GEngine->CompileShaderFromFile( WFileName, &Defines.at(0), "VS", "vs_4_0", &pVSBlob );
	if( FAILED( hr ) )
	{
		MessageBox( NULL,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
		assert(false);
	}

	// Create the vertex shader
	hr = GEngine->Device->CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &VertexShader );
	if( FAILED( hr ) )
	{	

		pVSBlob->Release();
		assert(false);
	}

	if(SKey.NumTex == 0)
	{
		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		UINT numElements = ARRAYSIZE( layout );

		// Create the input layout
		hr = GEngine->Device->CreateInputLayout( layout, numElements, pVSBlob->GetBufferPointer(),
			pVSBlob->GetBufferSize(), &VertexLayout );
	}
	else if(SKey.NumTex == 1)
	{
		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		UINT numElements = ARRAYSIZE( layout );

		// Create the input layout
		hr = GEngine->Device->CreateInputLayout( layout, numElements, pVSBlob->GetBufferPointer(),
			pVSBlob->GetBufferSize(), &VertexLayout );
	}
	

	pVSBlob->Release();
	if( FAILED( hr ) )
		assert(false);

	// Set the input layout

	// Compile the pixel shader
	ID3DBlob* pPSBlob = NULL;
	hr = GEngine->CompileShaderFromFile(WFileName, &Defines.at(0), "PS", "ps_4_0", &pPSBlob );
	if( FAILED( hr ) )
	{
		MessageBox( NULL,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
		assert(false);
	}

	// Create the pixel shader
	hr = GEngine->Device->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &PixelShader );
	pPSBlob->Release();
	if( FAILED( hr ) )
		assert(false);
}

void ShaderRes::SetShaderRes()
{
	GEngine->ImmediateContext->IASetInputLayout( VertexLayout );
	GEngine->ImmediateContext->VSSetShader( VertexShader, NULL, 0 );
	GEngine->ImmediateContext->PSSetShader( PixelShader, NULL, 0 );
}
