#include "browser-panel-internal.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

extern bool QueueCEFTask(std::function<void()> task);

namespace {
int modifiers(Qt::KeyboardModifiers keys, Qt::MouseButtons buttons = Qt::NoButton)
{
	int result = EVENTFLAG_NONE;
	if (keys & Qt::ShiftModifier)
		result |= EVENTFLAG_SHIFT_DOWN;
	if (keys & Qt::ControlModifier)
		result |= EVENTFLAG_CONTROL_DOWN;
	if (keys & Qt::AltModifier)
		result |= EVENTFLAG_ALT_DOWN;
	if (keys & Qt::MetaModifier)
		result |= EVENTFLAG_COMMAND_DOWN;
	if (keys & Qt::KeypadModifier)
		result |= EVENTFLAG_IS_KEY_PAD;
	if (buttons & Qt::LeftButton)
		result |= EVENTFLAG_LEFT_MOUSE_BUTTON;
	if (buttons & Qt::MiddleButton)
		result |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
	if (buttons & Qt::RightButton)
		result |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
	return result;
}

// CEF expects Windows virtual key codes even on Linux. Qt's nativeVirtualKey
// is an XKB keysym on Wayland, so it must not be passed through as a VK code.
int virtualKey(int key)
{
	if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
		return key;
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
		return 0x70 + key - Qt::Key_F1;
	switch (key) {
	case Qt::Key_Backspace:
		return 0x08;
	case Qt::Key_Tab:
	case Qt::Key_Backtab:
		return 0x09;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return 0x0d;
	case Qt::Key_Shift:
		return 0x10;
	case Qt::Key_Control:
		return 0x11;
	case Qt::Key_Alt:
		return 0x12;
	case Qt::Key_Escape:
		return 0x1b;
	case Qt::Key_Space:
		return 0x20;
	case Qt::Key_PageUp:
		return 0x21;
	case Qt::Key_PageDown:
		return 0x22;
	case Qt::Key_End:
		return 0x23;
	case Qt::Key_Home:
		return 0x24;
	case Qt::Key_Left:
		return 0x25;
	case Qt::Key_Up:
		return 0x26;
	case Qt::Key_Right:
		return 0x27;
	case Qt::Key_Down:
		return 0x28;
	case Qt::Key_Insert:
		return 0x2d;
	case Qt::Key_Delete:
		return 0x2e;
	case Qt::Key_Semicolon:
	case Qt::Key_Colon:
		return 0xba;
	case Qt::Key_Equal:
	case Qt::Key_Plus:
		return 0xbb;
	case Qt::Key_Comma:
	case Qt::Key_Less:
		return 0xbc;
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		return 0xbd;
	case Qt::Key_Period:
	case Qt::Key_Greater:
		return 0xbe;
	case Qt::Key_Slash:
	case Qt::Key_Question:
		return 0xbf;
	case Qt::Key_QuoteLeft:
	case Qt::Key_AsciiTilde:
		return 0xc0;
	case Qt::Key_BracketLeft:
	case Qt::Key_BraceLeft:
		return 0xdb;
	case Qt::Key_Backslash:
	case Qt::Key_Bar:
		return 0xdc;
	case Qt::Key_BracketRight:
	case Qt::Key_BraceRight:
		return 0xdd;
	case Qt::Key_Apostrophe:
	case Qt::Key_QuoteDbl:
		return 0xde;
	default:
		return 0;
	}
}

CefMouseEvent mouseEvent(QPointF point, Qt::KeyboardModifiers keys, Qt::MouseButtons buttons)
{
	CefMouseEvent event;
	event.x = qRound(point.x());
	event.y = qRound(point.y());
	event.modifiers = modifiers(keys, buttons);
	return event;
}
} // namespace

void QCefWidgetInternal::updateOSRGeometry()
{
	if (!osrState)
		return;
	// Throttle while dragging, rather than restarting a debounce indefinitely.
	if (!geometryTimer.isActive())
		geometryTimer.start();
	resizeSettledTimer.start();
}

void QCefWidgetInternal::flushOSRGeometry()
{
	if (!osrState)
		return;
	bool scaleChanged;
	{
		std::lock_guard<std::mutex> lock(osrState->mutex);
		scaleChanged = osrState->scale != devicePixelRatioF();
		osrState->width = std::max(1, width());
		osrState->height = std::max(1, height());
		osrState->scale = devicePixelRatioF();
		if (scaleChanged)
			osrState->recentViewports.clear();
		QSize viewport(osrState->width, osrState->height);
		if (osrState->recentViewports.empty() || osrState->recentViewports.front() != viewport)
			osrState->recentViewports.push_front(viewport);
		while (osrState->recentViewports.size() > 8)
			osrState->recentViewports.pop_back();
	}
	if (cefBrowser) {
		auto host = cefBrowser->GetHost();
		auto state = osrState;
		// At most one pending resize task per dock, even if CEF is busy.
		if (scaleChanged)
			state->screenInfoDirty = true;
		if (state->resizePending.exchange(true))
			return;
		if (!QueueCEFTask([host, state]() {
			state->resizePending = false;
			if (state->screenInfoDirty.exchange(false))
				host->NotifyScreenInfoChanged();
			host->WasResized();
			host->Invalidate(PET_VIEW);
		}))
			state->resizePending = false;
	}
}

void QCefWidgetInternal::paintEvent(QPaintEvent *event)
{
	if (!windowless) {
		QWidget::paintEvent(event);
		return;
	}
	QImage view, popup;
	CefRect popupRect;
	bool popupVisible;
	{
		std::lock_guard<std::mutex> lock(osrState->mutex);
		view = osrState->view;
		popup = osrState->popup;
		popupRect = osrState->popupRect;
		popupVisible = osrState->popupVisible;
	}
	QPainter painter(this);
	painter.fillRect(rect(), palette().window());
	PaintBrowserFrame(painter, QPointF(0, 0), view);
	if (popupVisible && !popup.isNull())
		PaintBrowserFrame(painter, QPointF(popupRect.x, popupRect.y), popup);
}

void QCefWidgetInternal::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	if (windowless) {
		paintTimer.stop();
		geometryTimer.stop();
		resizeSettledTimer.stop();
		if (cefBrowser) {
			auto host = cefBrowser->GetHost();
			QueueCEFTask([host]() { host->WasHidden(true); });
		}
	}
}

void QCefWidgetInternal::sendMouseClick(QMouseEvent *event, bool release, int count)
{
	if (!cefBrowser)
		return;
	CefBrowserHost::MouseButtonType button;
	switch (event->button()) {
	case Qt::LeftButton:
		button = MBT_LEFT;
		break;
	case Qt::MiddleButton:
		button = MBT_MIDDLE;
		break;
	case Qt::RightButton:
		button = MBT_RIGHT;
		break;
	default:
		return;
	}
	auto host = cefBrowser->GetHost();
	auto mouse = mouseEvent(event->position(), event->modifiers(), event->buttons());
	QueueCEFTask(
		[host, mouse, button, release, count]() { host->SendMouseClickEvent(mouse, button, release, count); });
	event->accept();
}

void QCefWidgetInternal::mousePressEvent(QMouseEvent *event)
{
	if (!windowless) {
		QWidget::mousePressEvent(event);
		return;
	}
	setFocus(Qt::MouseFocusReason);
	sendMouseClick(event, false, 1);
}

void QCefWidgetInternal::mouseReleaseEvent(QMouseEvent *event)
{
	if (!windowless) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	sendMouseClick(event, true, 1);
}

void QCefWidgetInternal::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (!windowless) {
		QWidget::mouseDoubleClickEvent(event);
		return;
	}
	sendMouseClick(event, false, 2);
}

void QCefWidgetInternal::mouseMoveEvent(QMouseEvent *event)
{
	if (!windowless) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	if (cefBrowser) {
		auto host = cefBrowser->GetHost();
		auto mouse = mouseEvent(event->position(), event->modifiers(), event->buttons());
		QueueCEFTask([host, mouse]() { host->SendMouseMoveEvent(mouse, false); });
	}
	event->accept();
}

void QCefWidgetInternal::leaveEvent(QEvent *event)
{
	QWidget::leaveEvent(event);
	if (windowless && cefBrowser) {
		auto host = cefBrowser->GetHost();
		auto mouse = mouseEvent(mapFromGlobal(QCursor::pos()), QApplication::keyboardModifiers(),
					QApplication::mouseButtons());
		QueueCEFTask([host, mouse]() { host->SendMouseMoveEvent(mouse, true); });
	}
}

void QCefWidgetInternal::wheelEvent(QWheelEvent *event)
{
	if (!windowless) {
		QWidget::wheelEvent(event);
		return;
	}
	if (cefBrowser) {
		auto host = cefBrowser->GetHost();
		auto mouse = mouseEvent(event->position(), event->modifiers(), event->buttons());
		QPoint delta = event->pixelDelta().isNull() ? event->angleDelta() : event->pixelDelta();
		QueueCEFTask([host, mouse, delta]() { host->SendMouseWheelEvent(mouse, delta.x(), delta.y()); });
	}
	event->accept();
}

void QCefWidgetInternal::sendKey(QKeyEvent *event, bool release)
{
	if (!cefBrowser)
		return;
	CefKeyEvent key;
	key.type = release ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
	key.windows_key_code = virtualKey(event->key());
	key.native_key_code = event->nativeScanCode();
	key.modifiers = modifiers(event->modifiers());
	key.is_system_key = (event->modifiers() & Qt::AltModifier) != 0;
	QString text = event->text();
	if (!text.isEmpty())
		key.character = key.unmodified_character = text.front().unicode();
	bool sendText = !release && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
	auto host = cefBrowser->GetHost();
	QueueCEFTask([host, key, text, sendText]() mutable {
		host->SendKeyEvent(key);
		if (sendText) {
			key.type = KEYEVENT_CHAR;
			for (QChar character : text) {
				key.character = key.unmodified_character = character.unicode();
				host->SendKeyEvent(key);
			}
		}
	});
	event->accept();
}

void QCefWidgetInternal::keyPressEvent(QKeyEvent *event)
{
	if (!windowless) {
		QWidget::keyPressEvent(event);
		return;
	}
	if (event->matches(QKeySequence::Paste)) {
		if (cefBrowser)
			PasteBrowserClipboard(cefBrowser->GetHost());
		event->accept();
		return;
	}
	sendKey(event, false);
}

void PasteBrowserClipboard(CefRefPtr<CefBrowserHost> host)
{
	// CEF's Linux clipboard may use X11 while Qt owns the Wayland clipboard.
	// Commit through the browser editor so selection, Unicode and undo work.
	std::u16string text = QApplication::clipboard()->text().toStdU16String();
	if (!text.empty())
		QueueCEFTask([host, text]() { host->ImeCommitText(text, CefRange(UINT32_MAX, UINT32_MAX), 0); });
}

void QCefWidgetInternal::keyReleaseEvent(QKeyEvent *event)
{
	if (!windowless) {
		QWidget::keyReleaseEvent(event);
		return;
	}
	sendKey(event, true);
}

void QCefWidgetInternal::focusInEvent(QFocusEvent *event)
{
	QWidget::focusInEvent(event);
	if (windowless && cefBrowser) {
		auto host = cefBrowser->GetHost();
		QueueCEFTask([host]() { host->SetFocus(true); });
	}
}

void QCefWidgetInternal::focusOutEvent(QFocusEvent *event)
{
	QWidget::focusOutEvent(event);
	if (windowless && cefBrowser) {
		auto host = cefBrowser->GetHost();
		QueueCEFTask([host]() { host->SetFocus(false); });
	}
}

void QCefWidgetInternal::inputMethodEvent(QInputMethodEvent *event)
{
	if (!windowless) {
		QWidget::inputMethodEvent(event);
		return;
	}
	if (cefBrowser && !event->commitString().isEmpty()) {
		auto host = cefBrowser->GetHost();
		std::u16string text = event->commitString().toStdU16String();
		QueueCEFTask([host, text]() { host->ImeCommitText(text, CefRange(UINT32_MAX, UINT32_MAX), 0); });
	}
	event->accept();
}

QVariant QCefWidgetInternal::inputMethodQuery(Qt::InputMethodQuery query) const
{
	if (windowless && query == Qt::ImEnabled)
		return true;
	return QWidget::inputMethodQuery(query);
}

bool QCefWidgetInternal::event(QEvent *event)
{
	if (windowless && event->type() == QEvent::ShortcutOverride) {
		event->accept();
		return true;
	}
	// QWidget normally consumes Tab for focus traversal before keyPressEvent.
	if (windowless && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
		auto key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
			sendKey(key, event->type() == QEvent::KeyRelease);
			return true;
		}
	}
	bool result = QWidget::event(event);
	if (windowless && event->type() == QEvent::ScreenChangeInternal)
		updateOSRGeometry();
	return result;
}
