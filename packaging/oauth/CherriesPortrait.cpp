#include "CherriesPortrait.hpp"
#include "CherriesPortraitGeometry.hpp"
#include <widgets/OBSQTDisplay.hpp>
#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <mutex>

namespace {
QString Uuid(obs_source_t *source)
{
	return source ? QString::fromUtf8(obs_source_get_uuid(source)) : QString();
}

class PortraitPreview : public OBSQTDisplay {
	std::mutex mutex;
	OBSCanvas canvas;
	OBSSceneItem selected;
	uint32_t canvasWidth = 1080, canvasHeight = 1920;
	QPointF press;
	obs_transform_info initial{};
	bool dragging = false, resizing = false;

	static void Draw(void *data, uint32_t cx, uint32_t cy)
	{
		auto self = static_cast<PortraitPreview *>(data);
		OBSCanvas canvas;
		OBSSceneItem selected;
		uint32_t canvasWidth, canvasHeight;
		{
			std::lock_guard lock(self->mutex);
			canvas = self->canvas;
			selected = self->selected;
			canvasWidth = self->canvasWidth;
			canvasHeight = self->canvasHeight;
		}
		if (!canvas || obs_canvas_removed(canvas))
			return;
		const auto view = CherriesPortraitGeometry::Fit(canvasWidth, canvasHeight, cx, cy);
		if (!view.scale)
			return;
		gs_viewport_push();
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_set_viewport(int(view.x), int(view.y), int(canvasWidth * view.scale),
				int(canvasHeight * view.scale));
		gs_ortho(0, float(canvasWidth), 0, float(canvasHeight), -100, 100);
		obs_canvas_render(canvas);
		if (selected && obs_sceneitem_get_scene(selected)) {
			matrix4 box;
			obs_sceneitem_get_box_transform(selected, &box);
			gs_matrix_mul(&box);
			auto effect = obs_get_base_effect(OBS_EFFECT_SOLID);
			gs_effect_set_color(gs_effect_get_param_by_name(effect, "color"), 0xFF48D9FF);
			while (gs_effect_loop(effect, "Solid")) {
				gs_render_start(true);
				gs_vertex2f(0, 0);
				gs_vertex2f(1, 0);
				gs_vertex2f(1, 1);
				gs_vertex2f(0, 1);
				gs_vertex2f(0, 0);
				gs_render_stop(GS_LINESTRIP);
			}
		}
		gs_matrix_pop();
		gs_projection_pop();
		gs_viewport_pop();
	}

	QPointF CanvasPoint(const QPointF &point) const
	{
		const auto view = CherriesPortraitGeometry::Fit(canvasWidth, canvasHeight, width(), height());
		return view.scale ? QPointF((point.x() - view.x) / view.scale, (point.y() - view.y) / view.scale)
				  : QPointF();
	}
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton || !canvas)
			return;
		press = CanvasPoint(event->position());
		OBSSourceAutoRelease source = obs_canvas_get_channel(canvas, 0);
		auto scene = obs_scene_from_source(source);
		struct Pick {
			QPointF point;
			obs_sceneitem_t *item = nullptr;
		} pick{press};
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
				auto pick = static_cast<Pick *>(data);
				if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
					return true;
				matrix4 box, inverse;
				obs_sceneitem_get_box_transform(item, &box);
				if (!matrix4_inv(&inverse, &box))
					return true;
				vec3 point;
				vec3_set(&point, float(pick->point.x()), float(pick->point.y()), 0);
				vec3_transform(&point, &point, &inverse);
				if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1)
					pick->item = item;
				return true;
			},
			&pick);
		SetSelected(pick.item);
		if (selectionChanged)
			selectionChanged(pick.item);
		if (!pick.item)
			return;
		obs_sceneitem_get_info2(pick.item, &initial);
		matrix4 box;
		obs_sceneitem_get_box_transform(pick.item, &box);
		vec3 corner;
		vec3_set(&corner, 1, 1, 0);
		vec3_transform(&corner, &corner, &box);
		const auto view = CherriesPortraitGeometry::Fit(canvasWidth, canvasHeight, width(), height());
		resizing = std::hypot(press.x() - corner.x, press.y() - corner.y) * view.scale < 14;
		dragging = true;
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (!dragging || !selected || !obs_sceneitem_get_scene(selected))
			return;
		if (!(event->buttons() & Qt::LeftButton)) {
			dragging = false;
			return;
		}
		const QPointF point = CanvasPoint(event->position());
		auto info = initial;
		if (resizing) {
			const double ratio = CherriesPortraitGeometry::ResizeRatio(press.x() - initial.pos.x,
										   press.y() - initial.pos.y,
										   point.x() - initial.pos.x,
										   point.y() - initial.pos.y);
			if (info.bounds_type != OBS_BOUNDS_NONE) {
				info.bounds.x *= ratio;
				info.bounds.y *= ratio;
			} else {
				info.scale.x *= ratio;
				info.scale.y *= ratio;
			}
		} else {
			info.pos.x += point.x() - press.x();
			info.pos.y += point.y() - press.y();
		}
		obs_sceneitem_set_info2(selected, &info);
	}
	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton && dragging) {
			dragging = false;
			obs_frontend_save();
		}
	}

public:
	std::function<void(obs_sceneitem_t *)> selectionChanged;
	explicit PortraitPreview(QWidget *parent) : OBSQTDisplay(parent)
	{
		setMinimumSize(160, 240);
		backgroundColor = 0xFF000000;
		connect(this, &OBSQTDisplay::DisplayCreated, this,
			[this]() { obs_display_add_draw_callback(GetDisplay(), Draw, this); });
	}
	~PortraitPreview() override
	{
		if (GetDisplay())
			obs_display_remove_draw_callback(GetDisplay(), Draw, this);
	}
	void SetCanvas(obs_canvas_t *value, uint32_t width, uint32_t height)
	{
		// Releasing a final OBS reference can acquire the graphics lock. Keep it outside our mutex.
		OBSCanvas previousCanvas;
		OBSSceneItem previousItem;
		{
			std::lock_guard lock(mutex);
			previousCanvas = std::move(canvas);
			previousItem = std::move(selected);
			dragging = false;
			canvas = value;
			canvasWidth = width;
			canvasHeight = height;
		}
	}
	void SetSelected(obs_sceneitem_t *item)
	{
		OBSSceneItem previousItem;
		{
			std::lock_guard lock(mutex);
			previousItem = std::move(selected);
			selected = item;
			dragging = false;
		}
	}
};

class PortraitDock : public QWidget {
	OBSCanvas canvas;
	OBSScene scene;
	OBSSceneItem selected;
	PortraitPreview *preview;
	QComboBox *scenes, *links, *resolution, *encoder;
	QSpinBox *bitrate;
	QCheckBox *follow;
	QListWidget *sources;
	QPushButton *create;
	QLabel *notice;
	QWidget *editor;
	QTimer refresh;
	bool closing = false, loading = false;
	QString canvasUuid, currentScene;
	int canvasWidth = 1080, canvasHeight = 1920;

	void Changed()
	{
		if (!loading && !closing)
			obs_frontend_save();
	}
	void Clear()
	{
		preview->SetCanvas(nullptr, canvasWidth, canvasHeight);
		selected = nullptr;
		scene = nullptr;
		canvas = nullptr;
		sources->clear();
		scenes->clear();
	}
	bool EnsureVideo(QString &error)
	{
		if (!canvas || obs_canvas_removed(canvas)) {
			error = "Add a portrait canvas from Docks → Portrait Canvas first.";
			return false;
		}
		obs_video_info video{}, current{};
		if (!obs_get_video_info(&video)) {
			error = "OBS video is not available.";
			return false;
		}
		video.base_width = video.output_width = canvasWidth;
		video.base_height = video.output_height = canvasHeight;
		video.output_format = VIDEO_FORMAT_NV12;
		video.colorspace = VIDEO_CS_709;
		video.range = VIDEO_RANGE_PARTIAL;
		if (obs_canvas_get_video_info(canvas, &current) && current.base_width == video.base_width &&
		    current.base_height == video.base_height && current.fps_num == video.fps_num &&
		    current.fps_den == video.fps_den)
			return true;
		if (!obs_canvas_reset_video(canvas, &video)) {
			error = "Stop streaming, recording, and the replay buffer before configuring the portrait canvas.";
			return false;
		}
		preview->SetCanvas(canvas, canvasWidth, canvasHeight);
		return true;
	}
	void RefreshScenes()
	{
		QSignalBlocker block(scenes);
		scenes->clear();
		if (canvas)
			obs_canvas_enum_scenes(
				canvas,
				[](void *data, obs_source_t *source) {
					auto combo = static_cast<QComboBox *>(data);
					if (obs_source_is_scene(source) && !obs_source_removed(source))
						combo->addItem(QString::fromUtf8(obs_source_get_name(source)),
							       Uuid(source));
					return true;
				},
				scenes);
		scenes->setCurrentIndex(scenes->findData(currentScene));
	}
	void SelectScene(const QString &uuid)
	{
		selected = nullptr;
		preview->SetSelected(nullptr);
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData());
		OBSCanvasAutoRelease owner = source ? obs_source_get_canvas(source) : nullptr;
		scene = owner == canvas ? obs_scene_from_source(source) : nullptr;
		currentScene = scene ? uuid : QString();
		if (canvas)
			obs_canvas_set_channel(canvas, 0, scene ? obs_scene_get_source(scene) : nullptr);
		RefreshScenes();
		RefreshSources();
		RefreshLinks();
		Changed();
	}
	void RefreshSources()
	{
		QSignalBlocker block(sources);
		const int64_t id = selected ? obs_sceneitem_get_id(selected) : -1;
		sources->clear();
		if (!scene)
			return;
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
				auto self = static_cast<PortraitDock *>(data);
				auto row = new QListWidgetItem(
					QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item))));
				row->setData(Qt::UserRole, qlonglong(obs_sceneitem_get_id(item)));
				row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
				row->setCheckState(obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
				self->sources->insertItem(0, row);
				return true;
			},
			this);
		for (int i = 0; i < sources->count(); ++i)
			if (sources->item(i)->data(Qt::UserRole).toLongLong() == id)
				sources->setCurrentRow(i);
	}
	void RefreshLinks()
	{
		QSignalBlocker block(links);
		links->clear();
		links->addItem("No linked landscape scene", QString());
		obs_frontend_source_list list{};
		obs_frontend_get_scenes(&list);
		for (size_t i = 0; i < list.sources.num; ++i)
			links->addItem(QString::fromUtf8(obs_source_get_name(list.sources.array[i])),
				       Uuid(list.sources.array[i]));
		obs_frontend_source_list_free(&list);
		OBSDataAutoRelease data = scene ? obs_source_get_private_settings(obs_scene_get_source(scene))
						: nullptr;
		links->setCurrentIndex(std::max(
			0, links->findData(QString::fromUtf8(obs_data_get_string(data, "cherries_landscape")))));
	}
	void LinkScene(obs_scene_t *target, const QString &uuid)
	{
		if (!target)
			return;
		if (!uuid.isEmpty()) {
			obs_canvas_enum_scenes(
				canvas,
				[](void *data, obs_source_t *source) {
					OBSDataAutoRelease settings = obs_source_get_private_settings(source);
					if (*static_cast<const QString *>(data) ==
					    QString::fromUtf8(obs_data_get_string(settings, "cherries_landscape")))
						obs_data_erase(settings, "cherries_landscape");
					return true;
				},
				const_cast<QString *>(&uuid));
		}
		OBSDataAutoRelease settings = obs_source_get_private_settings(obs_scene_get_source(target));
		obs_data_set_string(settings, "cherries_landscape", uuid.toUtf8().constData());
	}
	void FollowScene()
	{
		if (!follow->isChecked() || !canvas)
			return;
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		struct Find {
			QString uuid, portrait;
		} find{Uuid(program), {}};
		obs_canvas_enum_scenes(
			canvas,
			[](void *data, obs_source_t *source) {
				auto find = static_cast<Find *>(data);
				OBSDataAutoRelease settings = obs_source_get_private_settings(source);
				if (!find->uuid.isEmpty() &&
				    find->uuid ==
					    QString::fromUtf8(obs_data_get_string(settings, "cherries_landscape"))) {
					find->portrait = Uuid(source);
					return false;
				}
				return true;
			},
			&find);
		if (!find.portrait.isEmpty() && find.portrait != currentScene)
			SelectScene(find.portrait);
	}
	static void CopyItems(obs_scene_t *from, obs_scene_t *to)
	{
		obs_scene_enum_items(
			from,
			[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
				auto destination = static_cast<obs_scene_t *>(data);
				auto source = obs_sceneitem_get_source(item);
				obs_sceneitem_t *copy = nullptr;
				if (obs_source_is_group(source)) {
					copy = obs_scene_add_group(destination, obs_source_get_name(source));
					if (copy)
						CopyItems(obs_scene_from_source(source),
							  obs_scene_from_source(obs_sceneitem_get_source(copy)));
				} else {
					copy = obs_scene_add(destination, source);
				}
				if (copy) {
					obs_transform_info info{};
					obs_sceneitem_crop crop{};
					obs_sceneitem_get_info2(item, &info);
					obs_sceneitem_get_crop(item, &crop);
					obs_sceneitem_set_info2(copy, &info);
					obs_sceneitem_set_crop(copy, &crop);
					obs_sceneitem_set_visible(copy, obs_sceneitem_visible(item));
					obs_sceneitem_set_scale_filter(copy, obs_sceneitem_get_scale_filter(item));
					obs_sceneitem_set_blending_method(copy,
									  obs_sceneitem_get_blending_method(item));
					obs_sceneitem_set_blending_mode(copy, obs_sceneitem_get_blending_mode(item));
				}
				return true;
			},
			to);
	}
	void AddScene(bool copy)
	{
		if (!canvas)
			return;
		bool ok = false;
		const auto name = QInputDialog::getText(this, "Portrait scene", "Scene name", QLineEdit::Normal,
							copy ? "Portrait copy" : "Portrait scene", &ok)
					  .trimmed();
		if (!ok || name.isEmpty())
			return;
		OBSSceneAutoRelease added = obs_canvas_scene_create(canvas, name.toUtf8().constData());
		if (!added)
			return;
		if (copy) {
			OBSSourceAutoRelease source = obs_frontend_get_current_scene();
			if (auto landscape = obs_scene_from_source(source)) {
				CopyItems(landscape, added);
				const double scale =
					std::min(double(canvasWidth) / std::max(1u, obs_source_get_width(source)),
						 double(canvasHeight) / std::max(1u, obs_source_get_height(source)));
				obs_scene_enum_items(
					added,
					[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
						const double scale = *static_cast<const double *>(data);
						obs_transform_info info{};
						obs_sceneitem_get_info2(item, &info);
						info.pos.x *= scale;
						info.pos.y *= scale;
						info.scale.x *= scale;
						info.scale.y *= scale;
						info.bounds.x *= scale;
						info.bounds.y *= scale;
						obs_sceneitem_set_info2(item, &info);
						return true;
					},
					const_cast<double *>(&scale));
				LinkScene(added, Uuid(source));
			}
		}
		SelectScene(Uuid(obs_scene_get_source(added)));
	}
	void AddSource()
	{
		if (!scene)
			return;
		QComboBox choices;
		obs_enum_sources(
			[](void *data, obs_source_t *source) {
				auto choices = static_cast<QComboBox *>(data);
				if (!obs_source_removed(source) &&
				    (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
					choices->addItem(QString::fromUtf8(obs_source_get_name(source)), Uuid(source));
				return true;
			},
			&choices);
		QStringList names;
		for (int i = 0; i < choices.count(); ++i)
			names << choices.itemText(i);
		bool ok = false;
		const auto name =
			QInputDialog::getItem(this, "Reuse source", "Existing video source", names, 0, false, &ok);
		if (!ok || names.isEmpty())
			return;
		OBSSourceAutoRelease source =
			obs_get_source_by_uuid(choices.itemData(names.indexOf(name)).toString().toUtf8().constData());
		if (!source)
			return;
		selected = obs_scene_add(scene, source);
		FitSelected(false);
		RefreshSources();
		preview->SetSelected(selected);
		Changed();
	}
	void FitSelected(bool fill)
	{
		if (!selected || !obs_sceneitem_get_scene(selected))
			return;
		obs_transform_info info{};
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		info.scale.x = info.scale.y = 1;
		info.bounds_type = fill ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER;
		info.bounds.x = canvasWidth;
		info.bounds.y = canvasHeight;
		info.crop_to_bounds = fill;
		obs_sceneitem_crop crop{};
		obs_sceneitem_set_crop(selected, &crop);
		obs_sceneitem_set_info2(selected, &info);
		Changed();
	}
	void Transform()
	{
		if (!selected || !obs_sceneitem_get_scene(selected))
			return;
		OBSSceneItem item = selected;
		obs_transform_info info{};
		obs_sceneitem_crop crop{};
		obs_sceneitem_get_info2(item, &info);
		obs_sceneitem_get_crop(item, &crop);
		QDialog dialog(this);
		QTimer lifetime;
		lifetime.setInterval(100);
		connect(&lifetime, &QTimer::timeout, &dialog, [&]() {
			if (!obs_sceneitem_get_scene(item))
				dialog.reject();
		});
		lifetime.start();
		dialog.setWindowTitle("Portrait source transform");
		auto form = new QFormLayout(&dialog);
		auto field = [&](const char *label, double value, double min, double max) {
			auto box = new QDoubleSpinBox(&dialog);
			box->setRange(min, max);
			box->setDecimals(2);
			box->setValue(value);
			form->addRow(label, box);
			return box;
		};
		auto x = field("X", info.pos.x, -32000, 32000), y = field("Y", info.pos.y, -32000, 32000);
		auto sx = field("Scale X (%)", info.scale.x * 100, -10000, 10000);
		auto sy = field("Scale Y (%)", info.scale.y * 100, -10000, 10000);
		auto rotation = field("Rotation", info.rot, -360, 360);
		auto bounds = new QComboBox(&dialog);
		bounds->addItem("Scale directly", OBS_BOUNDS_NONE);
		bounds->addItem("Fit inside bounds", OBS_BOUNDS_SCALE_INNER);
		bounds->addItem("Fill bounds", OBS_BOUNDS_SCALE_OUTER);
		bounds->setCurrentIndex(std::max(0, bounds->findData(int(info.bounds_type))));
		form->addRow("Sizing", bounds);
		auto bw = field("Bounds width", info.bounds.x, 1, 16000),
		     bh = field("Bounds height", info.bounds.y, 1, 16000);
		auto source = obs_sceneitem_get_source(item);
		const int width = std::max(1u, obs_source_get_width(source));
		const int height = std::max(1u, obs_source_get_height(source));
		auto left = field("Crop left", crop.left, 0, width - 1),
		     right = field("Crop right", crop.right, 0, width - 1);
		auto top = field("Crop top", crop.top, 0, height - 1),
		     bottom = field("Crop bottom", crop.bottom, 0, height - 1);
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
		form->addRow(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() != QDialog::Accepted || !obs_sceneitem_get_scene(item))
			return;
		info.pos.x = x->value();
		info.pos.y = y->value();
		info.scale.x = sx->value() / 100;
		info.scale.y = sy->value() / 100;
		info.rot = rotation->value();
		info.bounds_type = obs_bounds_type(bounds->currentData().toInt());
		info.bounds.x = bw->value();
		info.bounds.y = bh->value();
		info.crop_to_bounds = info.bounds_type == OBS_BOUNDS_SCALE_OUTER;
		crop.left = left->value();
		crop.right = std::min(int(right->value()), width - crop.left - 1);
		crop.top = top->value();
		crop.bottom = std::min(int(bottom->value()), height - crop.top - 1);
		obs_sceneitem_set_crop(item, &crop);
		obs_sceneitem_set_info2(item, &info);
		Changed();
	}
	static void SaveLoad(obs_data_t *root, bool saving, void *data)
	{
		auto self = static_cast<PortraitDock *>(data);
		if (self->closing)
			return;
		if (saving) {
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_string(settings, "canvas", self->canvasUuid.toUtf8().constData());
			obs_data_set_string(settings, "scene", self->currentScene.toUtf8().constData());
			obs_data_set_int(settings, "width", self->canvasWidth);
			obs_data_set_int(settings, "height", self->canvasHeight);
			obs_data_set_int(settings, "bitrate", self->bitrate->value());
			obs_data_set_string(settings, "encoder",
					    self->encoder->currentData().toString().toUtf8().constData());
			obs_data_set_bool(settings, "follow", self->follow->isChecked());
			obs_data_set_obj(root, "cherries_portrait", settings);
		} else {
			self->loading = true;
			self->Clear();
			OBSDataAutoRelease settings = obs_data_get_obj(root, "cherries_portrait");
			self->canvasUuid = QString::fromUtf8(obs_data_get_string(settings, "canvas"));
			self->canvasWidth = obs_data_get_int(settings, "width") == 720 ? 720 : 1080;
			self->canvasHeight = self->canvasWidth == 720 ? 1280 : 1920;
			self->resolution->setCurrentIndex(self->canvasWidth == 720 ? 1 : 0);
			self->bitrate->setValue(obs_data_has_user_value(settings, "bitrate")
							? obs_data_get_int(settings, "bitrate")
							: 4500);
			const auto id = QString::fromUtf8(obs_data_get_string(settings, "encoder"));
			self->encoder->setCurrentIndex(std::max(0, self->encoder->findData(id)));
			self->follow->setChecked(!obs_data_has_user_value(settings, "follow") ||
						 obs_data_get_bool(settings, "follow"));
			OBSCanvasAutoRelease restored = obs_get_canvas_by_uuid(self->canvasUuid.toUtf8().constData());
			if (restored && !obs_canvas_removed(restored)) {
				self->canvas = restored;
				QString error;
				self->EnsureVideo(error);
				self->notice->setText(error);
				self->SelectScene(QString::fromUtf8(obs_data_get_string(settings, "scene")));
				if (!self->scene && self->scenes->count())
					self->SelectScene(self->scenes->itemData(0).toString());
			}
			self->loading = false;
			self->editor->setVisible(bool(self->canvas));
			self->create->setVisible(!self->canvas);
		}
	}
	static void Event(obs_frontend_event event, void *data)
	{
		auto self = static_cast<PortraitDock *>(data);
		if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP || event == OBS_FRONTEND_EVENT_EXIT) {
			self->loading = true;
			self->Clear();
			self->canvasUuid.clear();
			self->currentScene.clear();
			if (event == OBS_FRONTEND_EVENT_EXIT) {
				self->closing = true;
				self->refresh.stop();
				self->preview->DestroyDisplay();
			}
		} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
			self->FollowScene();
		} else if (event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED) {
			self->RefreshLinks();
		} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED ||
			   event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
			self->loading = false;
			QString error;
			if (self->canvas)
				self->EnsureVideo(error);
			self->notice->setText(error);
			self->FollowScene();
		}
	}

public:
	explicit PortraitDock(QWidget *main, obs_data_t *initial) : QWidget(main)
	{
		auto layout = new QVBoxLayout(this);
		create = new QPushButton("Add Portrait Canvas", this);
		layout->addWidget(create);
		notice = new QLabel(this);
		notice->setWordWrap(true);
		layout->addWidget(notice);
		editor = new QWidget(this);
		auto body = new QVBoxLayout(editor);
		body->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(editor);
		resolution = new QComboBox(editor);
		resolution->addItems({"1080 × 1920", "720 × 1280"});
		encoder = new QComboBox(editor);
		encoder->addItem("Use landscape encoder type", QString());
		const char *id = nullptr;
		for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
			if (obs_get_encoder_type(id) == OBS_ENCODER_VIDEO &&
			    strcmp(obs_get_encoder_codec(id), "h264") == 0 &&
			    !(obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)))
				encoder->addItem(QString::fromUtf8(obs_encoder_get_display_name(id)),
						 QString::fromUtf8(id));
		}
		bitrate = new QSpinBox(editor);
		bitrate->setRange(1000, 20000);
		bitrate->setSingleStep(500);
		bitrate->setValue(4500);
		bitrate->setSuffix(" Kbps");
		auto settings = new QFormLayout;
		settings->addRow("Portrait size", resolution);
		settings->addRow("Video encoder", encoder);
		settings->addRow("Video bitrate", bitrate);
		body->addLayout(settings);
		preview = new PortraitPreview(editor);
		body->addWidget(preview, 1);
		auto hint = new QLabel(
			"Drag to move; drag a source's bottom-right corner to resize. Double-click its name for cropping and transforms.",
			editor);
		hint->setWordWrap(true);
		body->addWidget(hint);
		scenes = new QComboBox(editor);
		body->addWidget(scenes);
		auto sceneButtons = new QHBoxLayout;
		auto addScene = new QPushButton("New scene", editor);
		auto copyScene = new QPushButton("Copy landscape", editor);
		auto deleteScene = new QPushButton("Remove scene", editor);
		for (auto button : {addScene, copyScene, deleteScene})
			sceneButtons->addWidget(button);
		body->addLayout(sceneButtons);
		follow = new QCheckBox("Follow linked landscape scene changes", editor);
		follow->setChecked(true);
		body->addWidget(follow);
		links = new QComboBox(editor);
		body->addWidget(links);
		sources = new QListWidget(editor);
		sources->setMaximumHeight(170);
		body->addWidget(sources);
		auto sourceButtons = new QHBoxLayout;
		auto addSource = new QPushButton("Reuse source", editor);
		auto transform = new QPushButton("Transform", editor);
		auto removeSource = new QPushButton("Remove", editor);
		for (auto button : {addSource, transform, removeSource})
			sourceButtons->addWidget(button);
		body->addLayout(sourceButtons);
		auto positionButtons = new QHBoxLayout;
		auto fit = new QPushButton("Fit", editor), fill = new QPushButton("Fill", editor);
		auto up = new QPushButton("Move up", editor), down = new QPushButton("Move down", editor);
		for (auto button : {fit, fill, up, down})
			positionButtons->addWidget(button);
		body->addLayout(positionButtons);
		connect(create, &QPushButton::clicked, this, [this]() {
			if (obs_video_active()) {
				QMessageBox::information(this, "Portrait canvas",
							 "Stop video outputs before adding a canvas.");
				return;
			}
			OBSCanvasAutoRelease added = obs_frontend_add_canvas("Portrait", nullptr, ACTIVATE | SCENE_REF);
			if (!added)
				return;
			canvas = added;
			canvasUuid = QString::fromUtf8(obs_canvas_get_uuid(canvas));
			QString error;
			if (!EnsureVideo(error))
				notice->setText(error);
			OBSSceneAutoRelease first = obs_canvas_scene_create(canvas, "Portrait scene");
			SelectScene(Uuid(obs_scene_get_source(first)));
			editor->show();
			create->hide();
			Changed();
		});
		connect(resolution, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int value) {
			if (loading)
				return;
			if (obs_video_active()) {
				QSignalBlocker block(resolution);
				resolution->setCurrentIndex(canvasWidth == 720 ? 1 : 0);
				return;
			}
			const int oldWidth = canvasWidth, oldHeight = canvasHeight;
			canvasWidth = value == 1 ? 720 : 1080;
			canvasHeight = value == 1 ? 1280 : 1920;
			QString error;
			if (!EnsureVideo(error)) {
				canvasWidth = oldWidth;
				canvasHeight = oldHeight;
				QSignalBlocker block(resolution);
				resolution->setCurrentIndex(oldWidth == 720 ? 1 : 0);
			}
			notice->setText(error);
			Changed();
		});
		connect(encoder, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { Changed(); });
		connect(bitrate, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { Changed(); });
		connect(scenes, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
			if (!loading)
				SelectScene(scenes->currentData().toString());
		});
		connect(addScene, &QPushButton::clicked, this, [this]() { AddScene(false); });
		connect(copyScene, &QPushButton::clicked, this, [this]() { AddScene(true); });
		connect(deleteScene, &QPushButton::clicked, this, [this]() {
			if (!scene || scenes->count() < 2)
				return;
			if (QMessageBox::question(
				    this, "Remove portrait scene",
				    "Remove this portrait scene? The shared capture sources will remain available.") !=
			    QMessageBox::Yes)
				return;
			OBSScene old = scene;
			SelectScene({});
			obs_source_remove(obs_scene_get_source(old));
			RefreshScenes();
			if (scenes->count())
				SelectScene(scenes->itemData(0).toString());
			Changed();
		});
		connect(follow, &QCheckBox::toggled, this, [this]() {
			if (!loading) {
				FollowScene();
				Changed();
			}
		});
		connect(links, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
			if (loading || !scene)
				return;
			LinkScene(scene, links->currentData().toString());
			Changed();
		});
		connect(addSource, &QPushButton::clicked, this, [this]() { AddSource(); });
		connect(transform, &QPushButton::clicked, this, [this]() { Transform(); });
		connect(sources, &QListWidget::itemDoubleClicked, this, [this]() { Transform(); });
		connect(sources, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *row) {
			selected = row && scene
					   ? obs_scene_find_sceneitem_by_id(scene, row->data(Qt::UserRole).toLongLong())
					   : nullptr;
			preview->SetSelected(selected);
		});
		connect(sources, &QListWidget::itemChanged, this, [this](QListWidgetItem *row) {
			if (auto item =
				    scene ? obs_scene_find_sceneitem_by_id(scene, row->data(Qt::UserRole).toLongLong())
					  : nullptr) {
				obs_sceneitem_set_visible(item, row->checkState() == Qt::Checked);
				Changed();
			}
		});
		preview->selectionChanged = [this](obs_sceneitem_t *item) {
			selected = item;
			RefreshSources();
		};
		connect(removeSource, &QPushButton::clicked, this, [this]() {
			if (!selected)
				return;
			obs_sceneitem_remove(selected);
			selected = nullptr;
			preview->SetSelected(nullptr);
			RefreshSources();
			Changed();
		});
		connect(fit, &QPushButton::clicked, this, [this]() { FitSelected(false); });
		connect(fill, &QPushButton::clicked, this, [this]() { FitSelected(true); });
		connect(up, &QPushButton::clicked, this, [this]() {
			if (selected) {
				obs_sceneitem_set_order(selected, OBS_ORDER_MOVE_UP);
				RefreshSources();
				Changed();
			}
		});
		connect(down, &QPushButton::clicked, this, [this]() {
			if (selected) {
				obs_sceneitem_set_order(selected, OBS_ORDER_MOVE_DOWN);
				RefreshSources();
				Changed();
			}
		});
		refresh.setInterval(500);
		connect(&refresh, &QTimer::timeout, this, [this]() {
			const bool active = obs_video_active();
			resolution->setEnabled(!active);
			encoder->setEnabled(!active);
			bitrate->setEnabled(!active);
			create->setEnabled(!active);
			if (canvas && obs_canvas_removed(canvas)) {
				loading = true;
				Clear();
				canvasUuid.clear();
				currentScene.clear();
				loading = false;
				editor->hide();
				create->show();
				Changed();
			}
		});
		refresh.start();
		obs_frontend_add_event_callback(Event, this);
		obs_frontend_add_save_callback(SaveLoad, this);
		SaveLoad(initial, false, this);
	}
	~PortraitDock() override
	{
		if (!closing) {
			obs_frontend_remove_event_callback(Event, this);
			obs_frontend_remove_save_callback(SaveLoad, this);
		}
	}
	bool Prepare(QString &error)
	{
		if (!EnsureVideo(error))
			return false;
		if (!scene || obs_source_removed(obs_scene_get_source(scene))) {
			error = "Select a scene in Docks → Portrait Canvas before starting.";
			return false;
		}
		return true;
	}
	video_t *Video() const
	{
		return canvas && !obs_canvas_removed(canvas) ? obs_canvas_get_video(canvas) : nullptr;
	}
	QString Encoder() const { return encoder->currentData().toString(); }
	int Bitrate() const { return bitrate->value(); }
};
QPointer<PortraitDock> portrait;
} // namespace

void CherriesInstallPortrait(QWidget *main, obs_data_t *collectionData)
{
	portrait = new PortraitDock(main, collectionData);
	obs_frontend_add_dock_by_id("cherriesPortraitCanvas", "Portrait Canvas", portrait);
	OBSDataAutoRelease saved = obs_data_get_obj(collectionData, "cherries_portrait");
	if (!*obs_data_get_string(saved, "canvas"))
		if (auto dock = qobject_cast<QDockWidget *>(portrait->parentWidget()))
			dock->hide();
	obs_frontend_add_tools_menu_item("Portrait Canvas", [](void *) { CherriesShowPortrait(); }, nullptr);
}
void CherriesShowPortrait()
{
	if (!portrait)
		return;
	if (auto dock = qobject_cast<QDockWidget *>(portrait->parentWidget())) {
		dock->show();
		dock->raise();
	}
}
bool CherriesPreparePortrait(QString &error)
{
	if (portrait)
		return portrait->Prepare(error);
	error = "The portrait canvas is not available.";
	return false;
}
video_t *CherriesPortraitVideo()
{
	return portrait ? portrait->Video() : nullptr;
}
QString CherriesPortraitEncoder()
{
	return portrait ? portrait->Encoder() : QString();
}
int CherriesPortraitBitrate()
{
	return portrait ? portrait->Bitrate() : 4500;
}

// Called before scene items load: relative coordinates need non-zero canvas dimensions.
void CherriesRestorePortraitVideo(obs_data_t *collectionData)
{
	OBSDataAutoRelease saved = obs_data_get_obj(collectionData, "cherries_portrait");
	const char *uuid = obs_data_get_string(saved, "canvas");
	if (!*uuid)
		return;
	OBSCanvasAutoRelease canvas = obs_get_canvas_by_uuid(uuid);
	obs_video_info video{};
	if (!canvas || obs_canvas_removed(canvas) || !obs_get_video_info(&video))
		return;
	video.base_width = video.output_width = obs_data_get_int(saved, "width") == 720 ? 720 : 1080;
	video.base_height = video.output_height = video.base_width == 720 ? 1280 : 1920;
	video.output_format = VIDEO_FORMAT_NV12;
	video.colorspace = VIDEO_CS_709;
	video.range = VIDEO_RANGE_PARTIAL;
	if (!obs_canvas_reset_video(canvas, &video))
		blog(LOG_WARNING, "[cherries-portrait] Could not restore the portrait canvas video mix");
}
