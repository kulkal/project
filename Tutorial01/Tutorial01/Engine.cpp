#include <cassert>
#include "Engine.h"
#include "SimpleDrawingPolicy.h"
#include "LineBatcher.h"
#include "GBufferDrawingPolicy.h"
#include "Texture2D.h"
#include "TextureDepth2D.h"

struct SCREEN_VERTEX
{
	XMFLOAT4 pos;
	XMFLOAT2 tex;
};

struct DeferredDirPSCBStruct
{
	XMFLOAT4 vLightDir;
	XMFLOAT4 vLightColor;
};

struct DeferredPointPSCBStruct
{
	XMFLOAT4 vLightPos;
	XMFLOAT4 vLightColor;
	XMMATRIX mView;
	XMMATRIX mProjection;
	XMFLOAT4 ProjectionParams;
};

struct VisDepthPSCBStruct
{
	XMMATRIX mView;
	XMMATRIX mProjection;
	XMFLOAT4 ProjectionParams;
};

Engine* GEngine;
Engine::Engine(void)
	:_hWnd(NULL)
	,_Device(NULL)
	,_ImmediateContext(NULL)
	,_DriverType(D3D_DRIVER_TYPE_NULL)
	,_FeatureLevel(D3D_FEATURE_LEVEL_11_0)
	,_SwapChain(NULL)
	//,_BackBuffer(NULL)
	//,_RenderTargetView(NULL)
	,_SimpleDrawer(NULL)
	,_LineBatcher(NULL)
	,_TimeSeconds(0.f)
	,_VisualizeWorldNormal(false)
	,_VisualizeDepth(false)
	,_DeferredDirPS(NULL)
	,_DeferredDirPSCB(NULL)
	,_DeferredPointPS(NULL)
	,_DeferredPointPSCB(NULL)
	,_VisNormalPS(NULL)
	,_VisDpethPS(NULL)
	,_VisDpethPSCB(NULL)
	,_DepthStateEnable(NULL)
	,_DepthStateDisable(NULL)
	,_WorldNormalTexture(NULL)
	,_DepthTexture(NULL)
	,_FrameBufferTexture(NULL)
	,_SceneColorTexture(NULL)
	,_LitTexture(NULL)
	,_CombineLitPS(NULL)
	
{
	_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	QueryPerformanceFrequency(&_Freq);
	QueryPerformanceCounter(&_PrevTime);
	_Near = 10.f;
	_Far = 500.f;
	//_CrtSetBreakAlloc(2486860);
}

Engine::~Engine(void)
{
	if( _ImmediateContext ) _ImmediateContext->ClearState();

	if(_DepthStateEnable) _DepthStateEnable->Release();
	if(_DepthStateDisable) _DepthStateDisable->Release();

	if( _SwapChain ) _SwapChain->Release();
	if( _ImmediateContext ) _ImmediateContext->Release();
	if( _Device ) _Device->Release();

	if(_VisNormalPS) _VisNormalPS->Release();
	if(_VisDpethPS) _VisDpethPS->Release();
	if(_VisDpethPSCB)_VisDpethPSCB->Release();
	
	if(g_pQuadVS) g_pQuadVS->Release();
	if(g_pScreenQuadVB) g_pScreenQuadVB->Release();
	if(g_pQuadLayout) g_pQuadLayout->Release();
	if(_DeferredDirPS) _DeferredDirPS->Release();
	if(_DeferredDirPSCB) _DeferredDirPSCB->Release();
	if(_DeferredPointPS) _DeferredPointPS->Release();
	if(_DeferredPointPSCB) _DeferredPointPSCB->Release();

	if(_SimpleDrawer) delete _SimpleDrawer;
	if(_GBufferDrawer) delete _GBufferDrawer;

	if(_LineBatcher) delete _LineBatcher;

	if(_WorldNormalTexture) delete _WorldNormalTexture;
	if(_DepthTexture) delete _DepthTexture;
	if(_FrameBufferTexture) delete _FrameBufferTexture;
	if(_SceneColorTexture) delete _SceneColorTexture;
	if(_LitTexture) delete _LitTexture;

	if(_CombineLitPS) _CombineLitPS->Release();

	for(int i=0;i<_BlendStateArray.size();i++)
	{
		ID3D11BlendState* BS = _BlendStateArray[i];
		BS->Release();
	}
}

void Engine::InitDevice()
{
	HRESULT hr;
	RECT rc;
	GetClientRect( _hWnd, &rc );
	_Width = rc.right - rc.left;
	_Height = rc.bottom - rc.top;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE( driverTypes );

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE( featureLevels );

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );
	sd.BufferCount = 1;
	sd.BufferDesc.Width = _Width;
	sd.BufferDesc.Height = _Height;
	sd.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = _hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	for( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ )
	{
		_DriverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDeviceAndSwapChain( NULL, _DriverType, NULL, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &sd, &_SwapChain, &_Device, &_FeatureLevel, &_ImmediateContext );
		if( SUCCEEDED( hr ) )
			break;
	}


	if( FAILED( hr ) )
		assert(false);

	// Create a render target view
	ID3D11Texture2D*		BackBuffer;
	hr = _SwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), ( LPVOID* )&BackBuffer );
	if( FAILED( hr ) )
		assert(false);

	_FrameBufferTexture = new Texture2D(BackBuffer, true);

	// scene color
	CD3D11_TEXTURE2D_DESC DescSceneColorTex(DXGI_FORMAT_R16G16B16A16_FLOAT, _Width, _Height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	CD3D11_SHADER_RESOURCE_VIEW_DESC DescSceneColorSRV(D3D11_SRV_DIMENSION_TEXTURE2D, DescSceneColorTex.Format, 0,  DescSceneColorTex.MipLevels);
	_SceneColorTexture = new Texture2D(DescSceneColorTex, DescSceneColorSRV, true);

	// lit
	CD3D11_TEXTURE2D_DESC DescLitTex(DXGI_FORMAT_R16G16B16A16_FLOAT, _Width, _Height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	CD3D11_SHADER_RESOURCE_VIEW_DESC DescLitSRV(D3D11_SRV_DIMENSION_TEXTURE2D, DescSceneColorTex.Format, 0, DescLitTex.MipLevels);
	_LitTexture = new Texture2D(DescLitTex, DescLitSRV, true);

	// world normal
	CD3D11_TEXTURE2D_DESC DescWordNormalTex(DXGI_FORMAT_R16G16B16A16_FLOAT, _Width, _Height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	CD3D11_SHADER_RESOURCE_VIEW_DESC DescWorldNormalSRV(D3D11_SRV_DIMENSION_TEXTURE2D, DescWordNormalTex.Format);
	_WorldNormalTexture = new Texture2D(DescWordNormalTex, DescWorldNormalSRV, true);

	// depth stencil texture
	CD3D11_TEXTURE2D_DESC DescDepthTex(DXGI_FORMAT_R24G8_TYPELESS, _Width, _Height, 1, 1, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
	CD3D11_DEPTH_STENCIL_VIEW_DESC  DescDSV(D3D11_DSV_DIMENSION_TEXTURE2D, DXGI_FORMAT_D24_UNORM_S8_UINT, 0, 0, 0,0) ;
	CD3D11_SHADER_RESOURCE_VIEW_DESC DescDepthSRV(D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
	_DepthTexture = new TextureDepth2D(DescDepthTex, DescDSV, DescDepthSRV);


	D3D11_DEPTH_STENCIL_DESC  DSStateDesc;
	ZeroMemory( &DSStateDesc, sizeof(DSStateDesc) );
	DSStateDesc.DepthEnable = TRUE;
    DSStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;

    DSStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    DSStateDesc.StencilEnable = FALSE;
	DSStateDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    DSStateDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    const D3D11_DEPTH_STENCILOP_DESC defaultStencilOp =
    { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
    DSStateDesc.FrontFace = defaultStencilOp;
	DSStateDesc.BackFace = defaultStencilOp;

    hr = _Device->CreateDepthStencilState(&DSStateDesc, &_DepthStateEnable);

	DSStateDesc.DepthEnable = FALSE;
    DSStateDesc.StencilEnable = FALSE;
	DSStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DSStateDesc.StencilWriteMask = 0x00;
    hr = _Device->CreateDepthStencilState(&DSStateDesc, &_DepthStateDisable);


	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)_Width;
	vp.Height = (FLOAT)_Height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	_ImmediateContext->RSSetViewports( 1, &vp );

	// prepare resources for full screen quad
	

	SCREEN_VERTEX svQuad[4];
	svQuad[0].pos = XMFLOAT4( -1.0f, 1.0f, 0.0f, 1.0f );
	svQuad[0].tex = XMFLOAT2( 0.0f, 0.0f );
	svQuad[2].pos = XMFLOAT4( 1.0f, 1.0f, 0.0f, 1.0f );
	svQuad[2].tex = XMFLOAT2( 1.0f, 0.0f );
	svQuad[1].pos = XMFLOAT4( -1.0f, -1.0f, 0.0f, 1.0f );
	svQuad[1].tex = XMFLOAT2( 0.0f, 1.0f );
	svQuad[3].pos = XMFLOAT4( 1.0f, -1.0f, 0.0f, 1.0f );
	svQuad[3].tex = XMFLOAT2( 1.0f, 1.0f );
	D3D11_BUFFER_DESC vbdesc =
	{
		4 * sizeof( SCREEN_VERTEX ),
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_VERTEX_BUFFER,
		0,
		0
	};
	D3D11_SUBRESOURCE_DATA InitData;
	InitData.pSysMem = svQuad;
	InitData.SysMemPitch = 0;
	InitData.SysMemSlicePitch = 0;
	_Device->CreateBuffer( &vbdesc, &InitData, &g_pScreenQuadVB ) ;

	ID3DBlob* pBlob = NULL;
	CompileShaderFromFile( L"QuadShader.fx", NULL, "QuadVS", "vs_4_0", &pBlob ) ;
	hr = _Device->CreateVertexShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pQuadVS ) ;
	if( FAILED( hr ) )
		assert(false);
	SetD3DResourceDebugName("QuadVS", g_pQuadVS);

	const D3D11_INPUT_ELEMENT_DESC quadlayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	hr = _Device->CreateInputLayout( quadlayout, 2, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &g_pQuadLayout ) ;
	if( FAILED( hr ) )
		assert(false);
	if(pBlob) pBlob->Release();
	SetD3DResourceDebugName("QuadLayout", g_pQuadLayout);

	D3D10_SHADER_MACRO DefinesVisNormal[] = {{"VIS_NORMAL", "1"},{0, 0} };
	
	ID3DBlob* pPSBlob = NULL;
	hr = CompileShaderFromFile(L"QuadShader.fx", DefinesVisNormal, "PS", "ps_4_0", &pPSBlob );
	if( FAILED( hr ) )
	{
		MessageBox( NULL,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
		assert(false);
	}

	hr = _Device->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &_VisNormalPS );
	pPSBlob->Release();
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName("_VisNormalPS", _VisNormalPS);

	D3D10_SHADER_MACRO DefinesVisDepth[] = {{"VIS_DEPTH", "1"},{0, 0} };
	
	pPSBlob = NULL;
	hr = CompileShaderFromFile(L"QuadShader.fx", DefinesVisDepth, "PS", "ps_4_0", &pPSBlob );
	if( FAILED( hr ) )
	{
		MessageBox( NULL,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
		assert(false);
	}

	hr = _Device->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &_VisDpethPS );
	pPSBlob->Release();
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName("_VisDpethPS", _VisDpethPS);
	
	D3D11_BUFFER_DESC bdc;
	ZeroMemory( &bdc, sizeof(bdc) );
	bdc.Usage = D3D11_USAGE_DEFAULT;
	bdc.ByteWidth = sizeof(VisDepthPSCBStruct);
	bdc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bdc.CPUAccessFlags = 0;
	hr = GEngine->_Device->CreateBuffer( &bdc, NULL, &_VisDpethPSCB );
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName("_VisDpethPSCB", _VisDpethPSCB);

	////////////////////////////////////////////
	// directional light
	_DeferredDirPS = CreatePixelShaderSimple("DeferredDirectional.fx");

	ZeroMemory( &bdc, sizeof(bdc) );
	bdc.Usage = D3D11_USAGE_DEFAULT;
	bdc.ByteWidth = sizeof(DeferredDirPSCBStruct);
	bdc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bdc.CPUAccessFlags = 0;
	hr = GEngine->_Device->CreateBuffer( &bdc, NULL, &_DeferredDirPSCB );
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName("_DeferredDirPSCB", _DeferredDirPSCB);

	// point light
	_DeferredPointPS = CreatePixelShaderSimple("DeferredPoint.fx");

	ZeroMemory( &bdc, sizeof(bdc) );
	bdc.Usage = D3D11_USAGE_DEFAULT;
	bdc.ByteWidth = sizeof(DeferredPointPSCBStruct);
	bdc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bdc.CPUAccessFlags = 0;
	hr = GEngine->_Device->CreateBuffer( &bdc, NULL, &_DeferredPointPSCB );
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName("_DeferredPointPSCB", _DeferredPointPSCB);
	
	_CombineLitPS = CreatePixelShaderSimple("ComblineShader.fx");

	InitDeviceStates();

	/////////////
	_SimpleDrawer = new SimpleDrawingPolicy;
	_GBufferDrawer = new GBufferDrawingPolicy;
	_LineBatcher = new LineBatcher;
	_LineBatcher->InitDevice();
}

ID3D11PixelShader* Engine::CreatePixelShaderSimple( char* szFileName, D3D10_SHADER_MACRO* pDefines)
{
	ID3D11PixelShader* PS;
	HRESULT hr;
	int nLen = strlen(szFileName)+1;
	wchar_t WFileName[1024];
	size_t RetSize;
	mbstowcs_s(&RetSize, WFileName, 1024, szFileName, nLen);

	ID3DBlob* pPSBlob = NULL;
	hr = CompileShaderFromFile(WFileName, pDefines, "PS", "ps_4_0", &pPSBlob );
	if( FAILED( hr ) )
	{
		MessageBox( NULL,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
		assert(false);
	}

	hr = _Device->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &PS );
	pPSBlob->Release();
	if( FAILED( hr ) )
		assert(false);

	SetD3DResourceDebugName(szFileName, PS);

	return PS;
}

void Engine::DrawFullScreenQuad11( ID3D11PixelShader* pPS, UINT Width, UINT Height, UINT TopLeftX, UINT TopLeftY)
{
	// Save the old viewport
	D3D11_VIEWPORT vpOld[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX];
	UINT nViewPorts = 1;
	_ImmediateContext->RSGetViewports( &nViewPorts, vpOld );

	// Setup the viewport to match the backbuffer
	D3D11_VIEWPORT vp;
	vp.Width = (float)Width;
	vp.Height = (float)Height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = TopLeftX;
	vp.TopLeftY = TopLeftY;
	_ImmediateContext->RSSetViewports( 1, &vp );

	UINT strides = sizeof( SCREEN_VERTEX );
	UINT offsets = 0;
	ID3D11Buffer* pBuffers[1] = { g_pScreenQuadVB };

	_ImmediateContext->IASetInputLayout( g_pQuadLayout );
	_ImmediateContext->IASetVertexBuffers( 0, 1, pBuffers, &strides, &offsets );
	_ImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );

	_ImmediateContext->VSSetShader( g_pQuadVS, NULL, 0 );
	_ImmediateContext->PSSetShader( pPS, NULL, 0 );
	_ImmediateContext->Draw( 4, 0 );

	// Restore the Old viewport
	_ImmediateContext->RSSetViewports( nViewPorts, vpOld );
}


HRESULT Engine::CompileShaderFromFile( WCHAR* szFileName, D3D10_SHADER_MACRO* pDefines, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
	HRESULT hr = S_OK;

	DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
	// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
	// Setting this flag improves the shader debugging experience, but still allows 
	// the shaders to be optimized and to run exactly the way they will run in 
	// the release configuration of this program.
	dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

	ID3DBlob* pErrorBlob;
	hr = D3DX11CompileFromFile( szFileName, pDefines, NULL, szEntryPoint, szShaderModel, 
		dwShaderFlags, 0, NULL, ppBlobOut, &pErrorBlob, NULL );
	if( FAILED(hr) )
	{
		if( pErrorBlob != NULL )
			OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
		if( pErrorBlob ) pErrorBlob->Release();
		return hr;
	}
	if( pErrorBlob ) pErrorBlob->Release();

	return S_OK;
}

void Engine::Tick()
{
	LARGE_INTEGER CurrentTime;

	QueryPerformanceCounter(&CurrentTime);
	_DeltaSeconds = (float)(CurrentTime.QuadPart - _PrevTime.QuadPart)/(float)_Freq.QuadPart;
	_TimeSeconds =	(float)(CurrentTime.QuadPart)/(float)_Freq.QuadPart;
	//cout_debug("delta seconds: %f\n", _DeltaSeconds);

	_PrevTime = CurrentTime;
}

void Engine::BeginRendering()
{
	ID3D11ShaderResourceView* aSRS[2] = {NULL, NULL};
	_ImmediateContext->PSSetShaderResources( 0, 2, aSRS );

	ID3D11RenderTargetView* aRTViews[ 2 ] = { _FrameBufferTexture->GetRTV(), _WorldNormalTexture->GetRTV() };
	_ImmediateContext->OMSetRenderTargets( 2, aRTViews, _DepthTexture->GetDepthStencilView() );     
    _ImmediateContext->OMSetDepthStencilState(_DepthStateEnable, 0);

	_LineBatcher->BeginLine();

	// Just clear the backbuffer
	float ClearColor[4] = { 0.f, 0.f, 0.f, 1.0f }; //red,green,blue,alpha
	_ImmediateContext->ClearRenderTargetView( _FrameBufferTexture->GetRTV(), ClearColor );
	float ClearNormalColor[4] = { 0.f, 0.f, 0.f, 1.0f }; //red,green,blue,alpha
	_ImmediateContext->ClearRenderTargetView( _WorldNormalTexture->GetRTV() , ClearNormalColor );
	_ImmediateContext->ClearDepthStencilView( _DepthTexture->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0 );

	// g-buffer pass
}
void Engine::EndRendering()
{
	_LineBatcher->Draw();
	ID3D11ShaderResourceView* aSRS[2] = {NULL, NULL};
	_ImmediateContext->VSSetShaderResources( 0, 2, aSRS );

	ID3D11RenderTargetView* aRTViews[ 1] = { _FrameBufferTexture->GetRTV() };
	_ImmediateContext->OMSetRenderTargets( 1, aRTViews, _DepthTexture->GetReadOnlyDepthStencilView() );     
    _ImmediateContext->OMSetDepthStencilState(_DepthStateDisable, 0);


	ID3D11ShaderResourceView* aSRV[2] = {_WorldNormalTexture->GetSRV(), _DepthTexture->GetSRV()};
	_ImmediateContext->PSSetShaderResources( 0, 2, aSRV );
	
	// lighting pass
	DeferredDirPSCBStruct cb;
	XMStoreFloat4(&cb.vLightDir, (XMVector4Normalize(XMLoadFloat4(&XMFLOAT4( -1, 0,-1, 1.0f )))));
	memcpy(&cb.vLightColor, &XMFLOAT4( 1.f, 0.f, 0.f, 1.f ), sizeof(XMFLOAT4));

	_ImmediateContext->UpdateSubresource( _DeferredDirPSCB, 0, NULL, &cb, 0, 0 );
	_ImmediateContext->PSSetConstantBuffers( 0, 1, &_DeferredDirPSCB );
	DrawFullScreenQuad11(GEngine->_DeferredDirPS, GEngine->_Width, GEngine->_Height);



	DeferredPointPSCBStruct cbPoint;
	XMStoreFloat4(&cbPoint.vLightPos, (XMVector4Normalize(XMLoadFloat4(&XMFLOAT4( 0, -100000, -0,5200 )))));
	//memcpy(&cb.vLightDir, &XMFLOAT4( -0.577f, 0.577f, 0.577f, 1.0f ), sizeof(XMFLOAT4));
	memcpy(&cbPoint.vLightColor, &XMFLOAT4( 1.f, 1.f, 1.f, 1.f ), sizeof(XMFLOAT4));

	cbPoint.mView = XMMatrixTranspose( XMLoadFloat4x4( &_ViewMat ));
	cbPoint.mProjection = XMMatrixTranspose( XMLoadFloat4x4(&GEngine->_ProjectionMat));

	cbPoint.ProjectionParams.x = _Far/(_Far - _Near);
	cbPoint.ProjectionParams.y = _Near/(_Near - _Far);
	cbPoint.ProjectionParams.z = _Far;
	cbPoint.ProjectionParams.w = _Near;
	_ImmediateContext->UpdateSubresource( _DeferredPointPSCB, 0, NULL, &cbPoint, 0, 0 );
	_ImmediateContext->PSSetConstantBuffers( 0, 1, &_DeferredPointPSCB );
	DrawFullScreenQuad11(GEngine->_DeferredPointPS, GEngine->_Width, GEngine->_Height);

	_VisualizeDepth = true;
	if(_VisualizeDepth)
	{
		VisDepthPSCBStruct cbVisDepth;
		cbVisDepth.mView = XMMatrixTranspose( XMLoadFloat4x4( &_ViewMat ));
		cbVisDepth.mProjection = XMMatrixTranspose( XMLoadFloat4x4(&GEngine->_ProjectionMat));
		cbVisDepth.ProjectionParams.x = _Far/(_Far - _Near);
		cbVisDepth.ProjectionParams.y = _Near/(_Near - _Far);
		cbVisDepth.ProjectionParams.z = _Far;
		cbVisDepth.ProjectionParams.w = _Near;


		_ImmediateContext->UpdateSubresource( _VisDpethPSCB, 0, NULL, &cbVisDepth, 0, 0 );
		_ImmediateContext->PSSetConstantBuffers( 0, 1, &_VisDpethPSCB );

		DrawFullScreenQuad11(_VisDpethPS, _Width/4, _Height/4, _Width*0.75, 0);
	}

	_VisualizeWorldNormal = true;
	if(_VisualizeWorldNormal)
	{
		DrawFullScreenQuad11(_VisNormalPS, _Width/4, _Height/4);
	}
	
	
	
	_SwapChain->Present( 0, 0 );
}

void Engine::InitDeviceStates()
{
	_BlendStateArray.resize(SIZE_BLENDSTATE);
	_BlendStateArray[BS_LIGHTING];

	CD3D11_BLEND_DESC DescBlend;

	_Device->CreateBlendState(&DescBlend, &_BlendStateArray[BS_NORMAL]);

	DescBlend.RenderTarget[0].BlendEnable = true;
	DescBlend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	DescBlend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	DescBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	DescBlend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	DescBlend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	DescBlend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

	_Device->CreateBlendState(&DescBlend, &_BlendStateArray[BS_LIGHTING]);

}


