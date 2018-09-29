#include "stdafx.h"
#include "HelloIndexBuffers.h"

HelloIndexBuffers::HelloIndexBuffers(UINT width, UINT height, wstring name) :
	m_width(width),
	m_height(height),
	m_title(name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_fenceValues{},
	m_rtvDescriptorSize(0)
{
}

HelloIndexBuffers::~HelloIndexBuffers()
{
}

void HelloIndexBuffers::OnInit()
{
	LoadPipeline();
	LoadResource();
}

// Update frame-based values
void HelloIndexBuffers::OnUpdate()
{

}

// Render the scene
void HelloIndexBuffers::OnRender()
{
	// record all the commands we need to render the scene into the command list
	PopulateCommandList();

	// execute the command list
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// present the frame
	ThrowIfFailed(m_swapChain->Present(1, 0));

	MoveToNextFrame();
}

void HelloIndexBuffers::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGPU();

	CloseHandle(m_fenceEvent);
}

// Load the rendering pipeline dependencies
void HelloIndexBuffers::LoadPipeline()
{
	// -- Create Direct3D Device -- //

	UINT dxgiFactoryFlags = 0;

	ComPtr<IDXGIFactory4> dxgiFactory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> hwAdapter;
	GetHardwareAdapter(dxgiFactory.Get(), &hwAdapter);

	// create the device
	ThrowIfFailed(D3D12CreateDevice(
		hwAdapter.Get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_device)
	));

	// -- Create RTV Command Queue -- //
	
	D3D12_COMMAND_QUEUE_DESC queueDesc = {}; // describe a direct command queue
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // direct means the gpu can directly execute this command queue

	// create the command queue
	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// -- Create Swap Chain -- //

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {}; // describe a swap chain
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width; // buffer width
	swapChainDesc.Height = m_height; // buffer height
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // format of the buffer (rgba 32 bits, 8 bits for each chanel)
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // this says the pipeline will render to this swap chain
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // dxgi will discard the buffer (data) after we call present
	swapChainDesc.SampleDesc.Count = 1; // describe our multi-sampling. We are not multi-sampling, so we set the count to 1 (we need at least one sample of course)

	ComPtr<IDXGISwapChain1> swapChain; 
	ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it
		Win32Application::GetHwnd(),
		&swapChainDesc, // give it the swap chain description we created above
		nullptr,
		nullptr,
		&swapChain // store the created swap chain in a temp IDXGISwapChain interface
	));

	// This sample does not support fullscreen transitions
	ThrowIfFailed(dxgiFactory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// -- Create Render Target Descriptor Heap -- //

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {}; // describe a render target view (RTV) descriptor heap
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // this heap will not be directly referenced by the shaders (not shader visible), as this will store the output from the pipeline
													       // otherwise we would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvDescriptorHeap)));

		// get the size of a descriptor in this heap (this is a rtv heap, so only rtv descriptors should be stored in it.
		// descriptor sizes may vary from device to device, which is why there is no set size and we must ask the 
		// device to give us the size. we will use this size to increment a descriptor handle offset
		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	{
		// get a handle to the first descriptor in the descriptor heap. a handle is basically a pointer,
		// but we cannot literally use it like a c++ pointer.
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		// create a RTV for each frame buffer (double buffering is two buffers, tripple buffering is 3).
		for (UINT n = 0; n < FrameCount; n++)
		{
			// first we get the n'th buffer in the swap chain and store it in the n'th position of our ID3D12Resource array
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			// we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtv handle
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			// we increment the rtv handle by the rtv descriptor size we got above
			rtvHandle.Offset(1, m_rtvDescriptorSize);

			// -- Create Command Allocators -- //
			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
		}
	}

	
}

// Load application resources
void HelloIndexBuffers::LoadResource()
{
	// -- Create Root Signature -- //

	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// -- Create Pipeline State -- //
	// which includes compiling and loading shaders

	{
		// create vertex and pixel shaders
		ComPtr<ID3DBlob> vertexShader; // d3d blob for holding vertex shader bytecode
		ComPtr<ID3DBlob> pixelShader; // d3d blob for holding pixel shader bytecode

		// Enable better shader debugging with the graphics debugging tools
	    // when debugging, we can compile the shader files at runtime.
		// but for release versions, we can compile the hlsl shaders
		// with fxc.exe to create .cso files, which contain the shader
		// bytecode. We can load the .cso files at runtime to get the
		// shader bytecode, which of course is faster than compiling
		// them at runtime
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

		// compile vertex and pixel shaders
		ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// create input layout
		// The input layout is used by the Input Assembler so that it knows
		// how to read the vertex data bound to it.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// describe a graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };  // the structure describing our input layout
		psoDesc.pRootSignature = m_rootSignature.Get(); // the root signature that describes the input data this pso needs
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get()); // structure describing where to find the vertex shader bytecode and how large it is
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get()); // same as VS but for pixel shader
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // a default rasterizer state
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // a default blent state
		psoDesc.SampleMask = UINT_MAX; // sample mask has to do with multi-sampling. 0xffffffff means point sampling is done
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // type of topology we are drawing
		psoDesc.NumRenderTargets = 1; // we are only binding one render target
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // format of the render target
		psoDesc.SampleDesc.Count = 1; // multisample count (no multisampling, so we just put 1, since we still need 1 sample)

		// create the pso
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// -- Create Command List -- //

	// create a command list with the first allocator
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());

	// -- Create Vertex Buffer -- //

	{
		// triangle vertices with color
		Vertex triangleVertices[] =
		{
			{ { -0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f  } },
			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { 0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }

		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		// create default heap to hold vertex buffer
		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view for the triangle
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress(); // get the GPU memory address to the vertex pointer
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;

		// -- Create Index Buffer -- //

		// a quad (two triangles)
		DWORD quadList[] = {
			0, 1 , 2, // first triangle (left-bottom)
			0, 3, 1 // second triangle (right-top)
		};

		const UINT indexBufferSize = sizeof(quadList);

		// create default heap to hold index buffer
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_indexBuffer)));

		// Copy the triangle data to the index buffer.
		UINT8* pIndexDataBegin;
		CD3DX12_RANGE readRangex(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_indexBuffer->Map(0, &readRangex, reinterpret_cast<void**>(&pIndexDataBegin)));
		memcpy(pIndexDataBegin, quadList, sizeof(quadList));
		m_indexBuffer->Unmap(0, nullptr);

		// Initialize the index buffer view for the triangle
		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress(); // get the GPU memory address to the vertex pointer
		m_indexBufferView.Format = DXGI_FORMAT_R32_UINT; // 32-bit unsigned integer (this is what a dword is, double word, a word is 2 bytes)
		m_indexBufferView.SizeInBytes = indexBufferSize;
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValues[m_frameIndex]++;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGPU();
	}
}

void HelloIndexBuffers::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->IASetIndexBuffer(&m_indexBufferView);
	m_commandList->DrawIndexedInstanced(6, 1, 0, 0, 0); // draw 2 triangles (draw 1 instance of 2 triangles)

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

void HelloIndexBuffers::WaitForGPU()
{
	// Schedule a Signal command in the queue
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

	// Wait until the previous frame is finished
	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame
	m_fenceValues[m_frameIndex]++;
}

void HelloIndexBuffers::MoveToNextFrame()
{
	// Schedule a Signal command in the queue
	const UINT64 fence = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));

	// Update the frame index
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame
	m_fenceValues[m_frameIndex] = fence + 1;
}

// Get the first available hardware adapter that supports Direct3D 12
void HelloIndexBuffers::GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
{
	ComPtr<IDXGIAdapter1> adapter; // the graphics card (this includes the embedded graphics on the motherboard)
	UINT adapterIndex = 0; // start looking for direct3d 12 compatible graphics devices starting at index 0

	*ppAdapter = nullptr;

	// find first hardware gpu that supports d3d 12
	while (pFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// we dont want a software device
			continue;
		}

		// check if the adapter supports Direct3D 12 (feature level 11 or higher), but don't create the actual device yet
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}

		adapterIndex++;
	}

	*ppAdapter = adapter.Detach();
}
