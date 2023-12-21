#include <include/lua.h>
#include <Windows.h>
#include <assert.h>
#include "../../main/win/main_win.h"

namespace LuaCore::D2D
{

	uint32_t get_hash(D2D1::ColorF color)
	{
		assert(color.r >= 0.0f && color.r <= 1.0f);
		assert(color.g >= 0.0f && color.g <= 1.0f);
		assert(color.b >= 0.0f && color.b <= 1.0f);
		assert(color.a >= 0.0f && color.a <= 1.0f);

		uint32_t r = static_cast<uint32_t>(color.r * 255.0f);
		uint32_t g = static_cast<uint32_t>(color.g * 255.0f);
		uint32_t b = static_cast<uint32_t>(color.b * 255.0f);
		uint32_t a = static_cast<uint32_t>(color.a * 255.0f);

		return (r << 24) | (g << 16) | (b << 8) | a;
	}

	ID2D1SolidColorBrush* d2d_get_cached_brush(LuaEnvironment* lua,
	                                           D2D1::ColorF color)
	{
		uint32_t key = get_hash(color);

		if (!lua->d2d_brush_cache.contains(key))
		{
			printf("Creating ID2D1SolidColorBrush (%f, %f, %f, %f) = %d\n",
			       color.r, color.g, color.b, color.a, key);

			ID2D1SolidColorBrush* brush;
			lua->d2d_render_target_stack.top()->CreateSolidColorBrush(
				color,
				&brush
			);
			lua->d2d_brush_cache[key] = brush;
		}

		return lua->d2d_brush_cache[key];
	}

#define D2D_GET_RECT(L, idx) D2D1::RectF( \
	luaL_checknumber(L, idx), \
	luaL_checknumber(L, idx + 1), \
	luaL_checknumber(L, idx + 2), \
	luaL_checknumber(L, idx + 3) \
)

#define D2D_GET_COLOR(L, idx) D2D1::ColorF( \
	luaL_checknumber(L, idx), \
	luaL_checknumber(L, idx + 1), \
	luaL_checknumber(L, idx + 2), \
	luaL_checknumber(L, idx + 3) \
)

#define D2D_GET_POINT(L, idx) D2D1_POINT_2F{ \
	.x = (float)luaL_checknumber(L, idx), \
	.y = (float)luaL_checknumber(L, idx + 1) \
}

#define D2D_GET_ELLIPSE(L, idx) D2D1_ELLIPSE{ \
	.point = D2D_GET_POINT(L, idx), \
	.radiusX = (float)luaL_checknumber(L, idx + 2), \
	.radiusY = (float)luaL_checknumber(L, idx + 3) \
}

#define D2D_GET_ROUNDED_RECT(L, idx) D2D1_ROUNDED_RECT( \
	D2D_GET_RECT(L, idx), \
	luaL_checknumber(L, idx + 5), \
	luaL_checknumber(L, idx + 6) \
)

	static int LuaD2DFillRectangle(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_RECT_F rectangle = D2D_GET_RECT(L, 1);
		D2D1::ColorF color = D2D_GET_COLOR(L, 5);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->FillRectangle(&rectangle, brush);

		return 0;
	}

	static int LuaD2DDrawRectangle(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_RECT_F rectangle = D2D_GET_RECT(L, 1);
		D2D1::ColorF color = D2D_GET_COLOR(L, 5);
		float thickness = luaL_checknumber(L, 9);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->DrawRectangle(&rectangle, brush, thickness);

		return 0;
	}

	static int LuaD2DFillEllipse(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_ELLIPSE ellipse = D2D_GET_ELLIPSE(L, 1);
		D2D1::ColorF color = D2D_GET_COLOR(L, 5);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->FillEllipse(&ellipse, brush);

		return 0;
	}

	static int LuaD2DDrawEllipse(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_ELLIPSE ellipse = D2D_GET_ELLIPSE(L, 1);
		D2D1::ColorF color = D2D_GET_COLOR(L, 5);
		float thickness = luaL_checknumber(L, 9);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->DrawEllipse(&ellipse, brush, thickness);

		return 0;
	}

	static int LuaD2DDrawLine(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_POINT_2F point_a = D2D_GET_POINT(L, 1);
		D2D1_POINT_2F point_b = D2D_GET_POINT(L, 3);
		D2D1::ColorF color = D2D_GET_COLOR(L, 5);
		float thickness = luaL_checknumber(L, 9);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->DrawLine(point_a, point_b, brush, thickness);

		return 0;
	}


static int LuaD2DDrawText(lua_State* L)
{
	LuaEnvironment* lua = GetLuaClass(L);

	D2D1_RECT_F rectangle = D2D_GET_RECT(L, 1);
	auto color = D2D_GET_COLOR(L, 5);

	// we would get tons of near-misses otherwise
	rectangle.left = (int)rectangle.left;
	rectangle.top = (int)rectangle.top;
	rectangle.right = (int)rectangle.right;
	rectangle.bottom = (int)rectangle.bottom;
	auto text = std::string(luaL_checkstring(L, 9));
	auto font_name = std::string(luaL_checkstring(L, 10));
	auto font_size = static_cast<float>(luaL_checknumber(L, 11));
	auto font_weight = static_cast<int>(luaL_checknumber(L, 12));
	auto font_style = static_cast<int>(luaL_checkinteger(L, 13));
	auto horizontal_alignment = static_cast<int>(luaL_checkinteger(L, 14));
	auto vertical_alignment = static_cast<int>(luaL_checkinteger(L, 15));

	ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

	int options = luaL_checkinteger(L, 16);


	IDWriteTextFormat* text_format;

	lua->dw_factory->CreateTextFormat(
		string_to_wstring(font_name).c_str(),
		nullptr,
		static_cast<DWRITE_FONT_WEIGHT>(font_weight),
		static_cast<DWRITE_FONT_STYLE>(font_style),
		DWRITE_FONT_STRETCH_NORMAL,
		font_size,
		L"",
		&text_format
	);

	text_format->SetTextAlignment(
		static_cast<DWRITE_TEXT_ALIGNMENT>(horizontal_alignment));
	text_format->SetParagraphAlignment(
		static_cast<DWRITE_PARAGRAPH_ALIGNMENT>(vertical_alignment));

	IDWriteTextLayout* text_layout;

	auto wtext = string_to_wstring(text);
	lua->dw_factory->CreateTextLayout(wtext.c_str(), wtext.length(),
	                                  text_format,
	                                  rectangle.right - rectangle.left,
	                                  rectangle.bottom - rectangle.top,
	                                  &text_layout);

	lua->d2d_render_target_stack.top()->DrawTextLayout({
			.x = rectangle.left,
			.y = rectangle.top,
		}, text_layout, brush,
		static_cast<
			D2D1_DRAW_TEXT_OPTIONS>(
			options));

	text_format->Release();
	text_layout->Release();
	return 0;
}

	static int LuaD2DSetTextAntialiasMode(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);
		float mode = luaL_checkinteger(L, 1);
		lua->d2d_render_target_stack.top()->SetTextAntialiasMode(
			(D2D1_TEXT_ANTIALIAS_MODE)mode);
		return 0;
	}

	static int LuaD2DSetAntialiasMode(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);
		float mode = luaL_checkinteger(L, 1);
		lua->d2d_render_target_stack.top()->SetAntialiasMode((D2D1_ANTIALIAS_MODE)mode);
		return 0;
	}

	static int LuaD2DGetTextSize(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		std::wstring text = string_to_wstring(std::string(luaL_checkstring(L, 1)));
		std::string font_name = std::string(luaL_checkstring(L, 2));
		float font_size = luaL_checknumber(L, 3);
		float max_width = luaL_checknumber(L, 4);
		float max_height = luaL_checknumber(L, 5);

		IDWriteTextFormat* text_format;

		lua->dw_factory->CreateTextFormat(
			string_to_wstring(font_name).c_str(),
			NULL,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			font_size,
			L"",
			&text_format
		);

		IDWriteTextLayout* text_layout;

		lua->dw_factory->CreateTextLayout(text.c_str(), text.length(),
		                                  text_format, max_width, max_height,
		                                  &text_layout);

		DWRITE_TEXT_METRICS text_metrics;
		text_layout->GetMetrics(&text_metrics);

		lua_newtable(L);
		lua_pushinteger(L, text_metrics.widthIncludingTrailingWhitespace);
		lua_setfield(L, -2, "width");
		lua_pushinteger(L, text_metrics.height);
		lua_setfield(L, -2, "height");

		text_format->Release();
		text_layout->Release();

		return 1;
	}

	static int LuaD2DPushClip(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_RECT_F rectangle = D2D_GET_RECT(L, 1);

		lua->d2d_render_target_stack.top()->PushAxisAlignedClip(
			rectangle,
			D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
		);

		return 0;
	}

	static int LuaD2DPopClip(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		lua->d2d_render_target_stack.top()->PopAxisAlignedClip();

		return 0;
	}

	static int LuaD2DFillRoundedRectangle(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_ROUNDED_RECT rounded_rectangle = D2D_GET_ROUNDED_RECT(L, 1);
		D2D1::ColorF color = D2D_GET_COLOR(L, 7);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->FillRoundedRectangle(&rounded_rectangle, brush);

		return 0;
	}

	static int LuaD2DDrawRoundedRectangle(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_ROUNDED_RECT rounded_rectangle = D2D_GET_ROUNDED_RECT(L, 1);
		D2D1::ColorF color = D2D_GET_COLOR(L, 7);
		float thickness = luaL_checknumber(L, 11);

		ID2D1SolidColorBrush* brush = d2d_get_cached_brush(lua, color);

		lua->d2d_render_target_stack.top()->DrawRoundedRectangle(
			&rounded_rectangle, brush, thickness);

		return 0;
	}

	static int LuaD2DLoadImage(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		std::string path(luaL_checkstring(L, 1));
		std::string identifier(luaL_checkstring(L, 2));

		if (lua->d2d_bitmap_cache.contains(identifier))
		{
			lua->d2d_bitmap_cache[identifier]->Release();
		}

		IWICImagingFactory* pIWICFactory = NULL;
		IWICBitmapDecoder* pDecoder = NULL;
		IWICBitmapFrameDecode* pSource = NULL;
		IWICFormatConverter* pConverter = NULL;
		ID2D1Bitmap* bmp = NULL;

		CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pIWICFactory)
		);

		HRESULT hr = pIWICFactory->CreateDecoderFromFilename(
			string_to_wstring(path).c_str(),
			NULL,
			GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,
			&pDecoder
		);

		if (!SUCCEEDED(hr))
		{
			printf("D2D image fail HRESULT %d\n", hr);
			pIWICFactory->Release();
			return 0;
		}

		pIWICFactory->CreateFormatConverter(&pConverter);
		pDecoder->GetFrame(0, &pSource);
		pConverter->Initialize(
			pSource,
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			NULL,
			0.0f,
			WICBitmapPaletteTypeMedianCut
		);

		lua->d2d_render_target_stack.top()->CreateBitmapFromWicBitmap(
			pConverter,
			NULL,
			&bmp
		);

		pIWICFactory->Release();
		pDecoder->Release();
		pSource->Release();
		pConverter->Release();

		lua->d2d_bitmap_cache[identifier] = bmp;

		return 0;
	}

	static int LuaD2DFreeImage(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		std::string identifier(luaL_checkstring(L, 1));
		lua->d2d_bitmap_cache[identifier]->Release();
		lua->d2d_bitmap_cache.erase(identifier);
		return 0;
	}

	static int LuaD2DDrawImage(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		D2D1_RECT_F destination_rectangle = D2D_GET_RECT(L, 1);
		D2D1_RECT_F source_rectangle = D2D_GET_RECT(L, 5);
		std::string identifier(luaL_checkstring(L, 9));
		float opacity = luaL_checknumber(L, 10);
		int interpolation = luaL_checkinteger(L, 11);

		ID2D1Bitmap* bitmap = lua->get_loose_bitmap(identifier.c_str());

		lua->d2d_render_target_stack.top()->DrawBitmap(
			bitmap,
			destination_rectangle,
			opacity,
			(D2D1_BITMAP_INTERPOLATION_MODE)interpolation,
			source_rectangle
		);

		return 0;
	}

	static int LuaD2DGetImageInfo(lua_State* L)
	{
		LuaEnvironment* lua = GetLuaClass(L);

		std::string identifier(luaL_checkstring(L, 1));
		ID2D1Bitmap* bitmap = lua->get_loose_bitmap(identifier.c_str());

		if (bitmap == nullptr) {
			luaL_error(L, "Bitmap doesnt exist");
		} else {
			D2D1_SIZE_U size = bitmap->GetPixelSize();
			lua_newtable(L);
			lua_pushinteger(L, size.width);
			lua_setfield(L, -2, "width");
			lua_pushinteger(L, size.height);
			lua_setfield(L, -2, "height");
		}

		return 1;
	}

	static int LuaD2DCreateRenderTarget(lua_State* L) {
		LuaEnvironment* lua = GetLuaClass(L);

		float width = luaL_checknumber(L, 1);
		float height = luaL_checknumber(L, 2);

		ID2D1BitmapRenderTarget* bitmap_render_target;
		lua->d2d_render_target_stack.top()->CreateCompatibleRenderTarget(D2D1::SizeF(width, height), &bitmap_render_target);

		std::string unique_key = "rt_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
		lua->d2d_bitmap_render_target[unique_key] = bitmap_render_target;

		lua_pushstring(L, unique_key.c_str());

		return 1;
	}

	static int LuaD2DDestroyRenderTarget(lua_State* L) {
		LuaEnvironment* lua = GetLuaClass(L);

		std::string key = luaL_checkstring(L, 1);

		if (lua->d2d_bitmap_render_target.contains(key)) {
			lua->d2d_bitmap_render_target[key]->Release();
			lua->d2d_bitmap_render_target.erase(key);
		}

		return 0;
	}

	static int LuaD2DBeginRenderTarget(lua_State* L) {
		LuaEnvironment* lua = GetLuaClass(L);

		std::string key = luaL_checkstring(L, 1);

		if (lua->d2d_bitmap_render_target.contains(key)) {
			lua->d2d_render_target_stack.push(lua->d2d_bitmap_render_target[key]);
			// we need to clear the brush cache because brushes are rt-scoped
			lua->d2d_brush_cache.clear();
			lua->d2d_bitmap_render_target[key]->BeginDraw();
		}

		return 0;
	}

	static int LuaD2DEndRenderTarget(lua_State* L) {
		LuaEnvironment* lua = GetLuaClass(L);

		std::string key = luaL_checkstring(L, 1);

		if (lua->d2d_bitmap_render_target.contains(key)) {
			lua->d2d_bitmap_render_target[key]->EndDraw();
			lua->d2d_render_target_stack.pop();
		}

		return 0;
	}

#undef D2D_GET_RECT
#undef D2D_GET_COLOR
#undef D2D_GET_POINT
#undef D2D_GET_ELLIPS
#undef D2D_GET_ROUNDED_RECT

}