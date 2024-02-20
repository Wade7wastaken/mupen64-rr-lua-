#pragma once
#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <cstdint>

static void set_checkbox_state(const HWND hwnd, const int32_t id,
                               int32_t is_checked)
{
	SendMessage(GetDlgItem(hwnd, id), BM_SETCHECK,
	            is_checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static int32_t get_checkbox_state(const HWND hwnd, const int32_t id)
{
	return SendDlgItemMessage(hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED
		       ? TRUE
		       : FALSE;
}

static int32_t get_primary_monitor_refresh_rate() {
	DISPLAY_DEVICE dd;
	dd.cb = sizeof(dd);
	EnumDisplayDevices(NULL, 0, &dd, 0);

	DEVMODE dm;
	dm.dmSize = sizeof(dm);
	EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm);

	return dm.dmDisplayFrequency;
}

static void read_combo_box_value(const HWND hwnd, const int resource_id, char* ret)
{
	int index = SendDlgItemMessage(hwnd, resource_id, CB_GETCURSEL, 0, 0);
	SendDlgItemMessage(hwnd, resource_id, CB_GETLBTEXT, index, (LPARAM)ret);
}

/**
 * \brief Accurately sleeps for the specified amount of time
 * \param seconds The seconds to sleep for
 * \remarks https://blat-blatnik.github.io/computerBear/making-accurate-sleep-function/
 */
static void accurate_sleep(double seconds) {
	using namespace std;
	using namespace std::chrono;

	static double estimate = 5e-3;
	static double mean = 5e-3;
	static double m2 = 0;
	static int64_t count = 1;

	while (seconds > estimate) {
		auto start = high_resolution_clock::now();
		this_thread::sleep_for(milliseconds(1));
		auto end = high_resolution_clock::now();

		double observed = (end - start).count() / 1e9;
		seconds -= observed;

		++count;
		double delta = observed - mean;
		mean += delta / count;
		m2   += delta * (observed - mean);
		double stddev = sqrt(m2 / (count - 1));
		estimate = mean + stddev;
	}

	// spin lock
	auto start = high_resolution_clock::now();
	while ((high_resolution_clock::now() - start).count() / 1e9 < seconds);
}


static RECT get_window_rect_client_space(HWND parent, HWND child)
{
	RECT offset_client = {0};
	MapWindowRect(child, parent, &offset_client);

	RECT client = {0};
	GetWindowRect(child, &client);

	return {
		offset_client.left,
		offset_client.top,
		offset_client.left + (client.right - client.left),
		offset_client.top + (client.bottom - client.top)
	};
}

static bool create_composition_surface(HWND hwnd, D2D1_SIZE_U size, IDCompositionDevice** comp_device, IDCompositionTarget** comp_target, IDXGISwapChain1** swapchain, ID2D1Factory3** d2d_factory, ID2D1Device2** d2d_device, ID3D11DeviceContext** d3d_dc, ID2D1DeviceContext2** d2d_dc)
{
	IDXGIFactory2* factory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

	IDXGIAdapter1* dxgiadapter;
	factory->EnumAdapters1(0, &dxgiadapter);

	ID3D11Device* d3device;
	D3D11CreateDevice(dxgiadapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3device, nullptr, d3d_dc);

	IDXGIDevice1* dxdevice;
	d3device->QueryInterface(&dxdevice);

	dxdevice->SetMaximumFrameLatency(1);

	{
		DCompositionCreateDevice(dxdevice, IID_PPV_ARGS(comp_device));

		(*comp_device)->CreateTargetForHwnd(hwnd, true, comp_target);

		IDCompositionVisual* compVisual;
		(*comp_device)->CreateVisual(&compVisual);

		DXGI_SWAP_CHAIN_DESC1 swapdesc{};
		swapdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapdesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		swapdesc.SampleDesc.Count = 1;
		swapdesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapdesc.BufferCount = 2;
		swapdesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapdesc.Width = size.width;
		swapdesc.Height = size.height;

		factory->CreateSwapChainForComposition(d3device, &swapdesc, nullptr, swapchain);

		compVisual->SetContent(*swapchain);
		(*comp_target)->SetRoot(compVisual);
	}

	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, {}, d2d_factory);
	(*d2d_factory)->CreateDevice(dxdevice, d2d_device);
	{
		(*d2d_device)->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2d_dc);

		IDXGISurface* surface;
		(*swapchain)->GetBuffer(0, IID_PPV_ARGS(&surface));

		const UINT dpi = GetDpiForWindow(hwnd);
		const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			dpi,
			dpi
		);

		ID2D1Bitmap1* bitmap;
		(*d2d_dc)->CreateBitmapFromDxgiSurface(surface, props, &bitmap);
		(*d2d_dc)->SetTarget(bitmap);
	}

	return true;
}
