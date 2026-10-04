#pragma once

#include "cef-headers.hpp"
#include "browser-panel-frame.hpp"
#include <QImage>
#include <atomic>
#include <memory>
#include <mutex>

// Shared pixel/geometry state only: the CEF UI thread never calls QWidget here.
struct QCefOSRState {
	std::mutex mutex;
	QImage view;
	QImage popup;
	CefRect popupRect;
	bool popupVisible = false;
	bool dirty = false;
	int width = 1;
	int height = 1;
	float scale = 1.0f;
	std::atomic<cef_cursor_type_t> cursor{CT_POINTER};
};

class QCefOSRRenderHandler : public CefRenderHandler {
public:
	explicit QCefOSRRenderHandler(std::shared_ptr<QCefOSRState> state_) : state(std::move(state_)) {}
	void setCursor(cef_cursor_type_t type) { state->cursor = type; }

	void GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) override
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		rect = CefRect(0, 0, state->width, state->height);
	}

	bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo &info) override
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		info.device_scale_factor = state->scale;
		info.depth = 32;
		info.depth_per_component = 8;
		info.is_monochrome = false;
		info.rect = info.available_rect = CefRect(0, 0, state->width, state->height);
		return true;
	}

	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList &, const void *buffer, int width,
		     int height) override
	{
		if (!buffer || width <= 0 || height <= 0)
			return;
		std::lock_guard<std::mutex> lock(state->mutex);
		QSize logicalSize = type == PET_POPUP ? QSize(state->popupRect.width, state->popupRect.height)
						      : QSize(state->width, state->height);
		// CEF owns buffer only for this callback. Copy before returning, with
		// geometry and DPI from one coherent viewport snapshot.
		QImage frame = CopyBrowserFrame(buffer, QSize(width, height), logicalSize, state->scale);
		if (frame.isNull())
			return;
		(type == PET_POPUP ? state->popup : state->view) = std::move(frame);
		state->dirty = true;
	}

	void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->popupVisible = show;
		if (!show)
			state->popup = QImage();
		state->dirty = true;
	}

	void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect &rect) override
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->popupRect = rect;
		state->dirty = true;
	}

private:
	std::shared_ptr<QCefOSRState> state;
	IMPLEMENT_REFCOUNTING(QCefOSRRenderHandler);
};
