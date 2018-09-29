#include "stdafx.h"
#include "HelloTriangle.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	HelloTriangle sample(800, 600, L"D3D12 Hello Triangle");
	return Win32Application::Run(&sample, hInstance, nCmdShow);
}
