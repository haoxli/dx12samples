#pragma once

#include "DXSampleHelper.h"
#include "Win32Application.h"

using namespace std;
using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class HelloIndexBuffers
{
public:
	HelloIndexBuffers(UINT width, UINT height, wstring name);
	~HelloIndexBuffers();

	void OnInit();
	void OnUpdate();
	void OnRender();
	void OnDestroy();

	// Accessors
	UINT GetWidth() const { return m_width; }
	UINT GetHeight() const { return m_height; }
	const WCHAR* GetTitle() const { return m_title.c_str(); }

protected:
	void GetHardwareAdapter(_In_ IDXGIFactory2* pFactory, _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter);

	// Viewport dimensions
	UINT m_width; // window width
	UINT m_height; // window height

	wstring m_title; // window title

private:
	static const UINT FrameCount = 3; // number of buffers we want, 2 for double buffering, 3 for tripple buffering

	// vertex structure
	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
	};  

	// Graphics Pipeline objects
	CD3DX12_VIEWPORT m_viewport; // area that output from rasterizer will be stretched to
	CD3DX12_RECT m_scissorRect; // the area to draw in pixels outside that area will not be drawn onto
	ComPtr<IDXGISwapChain3> m_swapChain; // swapchain used to switch between render targets
	ComPtr<ID3D12Device> m_device; // direct3d device
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount]; // number of render targets equal to buffer count
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount]; // we want enough allocators for each buffer * number of threads (we only have one thread)
	ComPtr<ID3D12CommandQueue> m_commandQueue; // container for command lists
	ComPtr<ID3D12RootSignature> m_rootSignature; // root signature defines data shaders will access
	ComPtr<ID3D12DescriptorHeap> m_rtvDescriptorHeap; // a descriptor heap to hold resources like the render targets
	ComPtr<ID3D12PipelineState> m_pipelineState; // pso containing a pipeline state
	ComPtr<ID3D12GraphicsCommandList> m_commandList; // a command list we can record commands into, then execute them to render the frame
	UINT m_rtvDescriptorSize; // size of the rtv descriptor on the device (all front and back buffers will be the same size) function declarations

	// App resources
	ComPtr<ID3D12Resource> m_vertexBuffer; // a default buffer in GPU memory that we will load vertex data for our triangle into
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView; // a structure containing a pointer to the vertex data in gpu memory
										         // the total size of the buffer, and the size of each element (vertex)

	ComPtr<ID3D12Resource> m_indexBuffer; // a default buffer in GPU memory that we will load index data for our triangle into
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView; // a structure holding information about the index buffer

	// Synchronization objects
	UINT m_frameIndex; // current rtv we are on
	HANDLE m_fenceEvent; // a handle to an event when our fence is unlocked by the gpu
	ComPtr<ID3D12Fence> m_fence; // an object that is locked while our command list is being executed by the gpu. We need as many 
							     // as we have allocators (more if we want to know when the gpu is finished with an asset)
	UINT64 m_fenceValues[FrameCount]; // this values are incremented each frame. each fence will have its own value

	void LoadPipeline();
	void LoadResource();
	void PopulateCommandList();
	void MoveToNextFrame();
	void WaitForGPU();
};
