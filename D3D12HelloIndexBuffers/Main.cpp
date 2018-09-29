#include "stdafx.h"
#include "HelloIndexBuffers.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	HelloIndexBuffers sample(800, 600, L"D3D12 Hello Index Buffers");
	return Win32Application::Run(&sample, hInstance, nCmdShow);
}
