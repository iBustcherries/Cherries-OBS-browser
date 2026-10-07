// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Cherries OBS contributors");

class Studio : public QObject {
    QMainWindow *main;
    QPointer<QDockWidget> vertical, destinations, pairs, clips;
    QTableWidget *table = nullptr;
    QCheckBox *linked = nullptr;
    QString sceneSignature;
    QTimer refresh;

    QDockWidget *dock(const char *name) const { return main->findChild<QDockWidget *>(name); }
    QObject *canvas() const { return vertical ? vertical->widget() : nullptr; }
    int width() const { return canvas() ? canvas()->property("cherriesWidth").toInt() : 1080; }
    int height() const { return canvas() ? canvas()->property("cherriesHeight").toInt() : 1920; }
    void invoke(const char *slot) {
        if (auto c = canvas()) QMetaObject::invokeMethod(c, slot, Qt::QueuedConnection);
    }
    void reveal(QDockWidget *d) { if (d) { d->show(); d->raise(); } }
    QPushButton *button(QBoxLayout *layout, const QString &text, std::function<void()> action) {
        auto b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, std::move(action));
        layout->addWidget(b); return b;
    }
    QString getLink(obs_source_t *source) {
        auto s = obs_source_get_settings(source);
        auto a = obs_data_get_array(s, "canvas");
        QString result;
        for (size_t i=0; i<obs_data_array_count(a); ++i) {
            auto entry = obs_data_array_item(a,i);
            if (obs_data_get_int(entry,"width")==width() && obs_data_get_int(entry,"height")==height())
                result = QString::fromUtf8(obs_data_get_string(entry,"scene"));
            obs_data_release(entry);
        }
        obs_data_array_release(a); obs_data_release(s); return result;
    }
    void setLink(const QString &uuid, const QString &name) {
        auto source = obs_get_source_by_uuid(uuid.toUtf8().constData());
        if (!source) return;
        auto s = obs_source_get_settings(source);
        auto a = obs_data_get_array(s,"canvas");
        if (!a) a = obs_data_array_create();
        for (size_t i=obs_data_array_count(a); i>0; --i) {
            auto entry = obs_data_array_item(a,i-1);
            if (obs_data_get_int(entry,"width")==width() && obs_data_get_int(entry,"height")==height())
                obs_data_array_erase(a,i-1);
            obs_data_release(entry);
        }
        if (!name.isEmpty()) {
            auto entry = obs_data_create();
            obs_data_set_int(entry,"width",width()); obs_data_set_int(entry,"height",height());
            obs_data_set_string(entry,"scene",name.toUtf8().constData());
            obs_data_array_push_back(a,entry); obs_data_release(entry);
        }
        obs_data_set_array(s,"canvas",a);
        obs_data_array_release(a); obs_data_release(s); obs_source_release(source);
        obs_frontend_save(); invoke("MainSceneChanged"); sceneSignature.clear();
    }
    void updatePairs() {
        if (!table || !canvas()) return;
        auto config = obs_frontend_get_profile_config();
        config_set_default_bool(config,"CherriesStudio","LinkScenes",true);
        linked->blockSignals(true);
        linked->setChecked(config_get_bool(config,"CherriesStudio","LinkScenes"));
        linked->blockSignals(false);
        QStringList portraitNames;
        auto c = obs_get_canvas_by_name("Aitum Vertical");
        if (c) obs_canvas_enum_scenes(c, [](void *data, obs_source_t *s) {
            static_cast<QStringList *>(data)->append(QString::fromUtf8(obs_source_get_name(s))); return true;
        }, &portraitNames);
        obs_canvas_release(c);
        obs_frontend_source_list list = {};
        obs_frontend_get_scenes(&list);
        QString signature = QString::number(width()) + "/" + QString::number(height()) + portraitNames.join('\n');
        for (size_t i=0;i<list.sources.num;++i) signature += QString::fromUtf8(obs_source_get_uuid(list.sources.array[i])) +
            QString::fromUtf8(obs_source_get_name(list.sources.array[i])) + getLink(list.sources.array[i]);
        if (signature != sceneSignature && !table->hasFocus() && !table->isAncestorOf(QApplication::focusWidget())) {
            sceneSignature = signature;
            table->setRowCount(int(list.sources.num));
            for (size_t i=0;i<list.sources.num;++i) {
                auto source = list.sources.array[i];
                const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
                auto item = new QTableWidgetItem(QString::fromUtf8(obs_source_get_name(source)));
                item->setData(Qt::UserRole, uuid); item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                table->setItem(int(i),0,item);
                auto choice = new QComboBox(table); choice->addItem("Independent", "");
                for (const auto &name : portraitNames) choice->addItem(name,name);
                choice->setCurrentIndex(qMax(0,choice->findData(getLink(source))));
                table->setCellWidget(int(i),1,choice);
                connect(choice,qOverload<int>(&QComboBox::activated),this,[this,choice,uuid](int){setLink(uuid,choice->currentData().toString());});
            }
        }
        obs_frontend_source_list_free(&list);
    }
    void arrange() {
        if (vertical) {
            main->addDockWidget(Qt::RightDockWidgetArea,vertical);
            vertical->setFloating(false); vertical->show();
        }
        if (destinations) {
            main->addDockWidget(Qt::RightDockWidgetArea,destinations);
            if (vertical) main->splitDockWidget(vertical,destinations,Qt::Horizontal);
            destinations->setFloating(false); destinations->show();
        }
        auto scenes = dock("scenesDock"); auto sources = dock("sourcesDock");
        auto portraitScenes = dock("VerticalCanvasDockScenes");
        auto portraitSources = dock("VerticalCanvasDockSources");
        auto transitions = dock("transitionsDock"); auto portraitTransitions = dock("VerticalCanvasDockTransitions");
        if (scenes && portraitScenes) main->tabifyDockWidget(scenes,portraitScenes);
        if (sources && portraitSources) { main->tabifyDockWidget(sources,portraitSources); sources->raise(); }
        if (transitions && portraitTransitions) main->tabifyDockWidget(transitions,portraitTransitions);
        if (pairs) {
            main->addDockWidget(Qt::BottomDockWidgetArea,pairs);
            if (scenes) main->tabifyDockWidget(scenes,pairs);
            pairs->show(); pairs->raise();
        }
        if (clips) { main->addDockWidget(Qt::BottomDockWidgetArea,clips); clips->show(); }
        if (vertical && destinations) main->resizeDocks({vertical,destinations},{300,330},Qt::Horizontal);
    }
public:
    explicit Studio(QMainWindow *window) : QObject(window), main(window) {
        setObjectName("cherriesStudioController");
        vertical = dock("VerticalCanvasDock"); destinations = dock("AitumMultistreamDock");
        if (!vertical || !destinations) {
            blog(LOG_ERROR,"[Cherries Studio] Required canvas/output engine did not load"); return;
        }
        vertical->setWindowTitle("Portrait Canvas"); destinations->setWindowTitle("Destinations");
        if (auto d=dock("VerticalCanvasDockScenes")) d->setWindowTitle("Portrait Scenes");
        if (auto d=dock("VerticalCanvasDockSources")) d->setWindowTitle("Portrait Sources");
        if (auto d=dock("sourcesDock")) d->setWindowTitle("Landscape Sources");
        auto toolbar = new QToolBar("Cherries Studio",main);
        toolbar->setObjectName("cherriesStudioToolbar"); toolbar->setMovable(false);
        main->addToolBar(Qt::TopToolBarArea,toolbar);
        toolbar->addWidget(new QLabel("  Cherries Studio  "));
        auto a=toolbar->addAction("Canvases"); connect(a,&QAction::triggered,this,[this]{reveal(vertical);});
        a=toolbar->addAction("Destinations"); connect(a,&QAction::triggered,this,[this]{reveal(destinations);});
        a=toolbar->addAction("Recording"); connect(a,&QAction::triggered,this,[this]{reveal(clips); invoke("ConfigButtonClicked");});
        a=toolbar->addAction("Settings"); connect(a,&QAction::triggered,this,[this]{QMetaObject::invokeMethod(main,"on_action_Settings_triggered",Qt::QueuedConnection);});
        a=toolbar->addAction("Portrait Settings"); connect(a,&QAction::triggered,this,[this]{invoke("ConfigButtonClicked");});

        auto pairWidget = new QWidget;
        auto pairLayout = new QVBoxLayout(pairWidget); pairLayout->setContentsMargins(4,4,4,4);
        linked = new QCheckBox("Link scene changes",pairWidget);
        linked->setChecked(config_get_bool(obs_frontend_get_profile_config(),"CherriesStudio","LinkScenes"));
        connect(linked,&QCheckBox::toggled,this,[this](bool on){
            auto c=obs_frontend_get_profile_config(); config_set_bool(c,"CherriesStudio","LinkScenes",on); config_save_safe(c,"tmp",nullptr);
            if(on) invoke("MainSceneChanged");
        });
        pairLayout->addWidget(linked);
        table = new QTableWidget(0,2,pairWidget); table->setObjectName("cherriesScenePairs");
        table->setHorizontalHeaderLabels({"Landscape","Portrait"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->hide(); pairLayout->addWidget(table);
        connect(table,&QTableWidget::cellDoubleClicked,this,[this](int row,int){
            if (auto item=table->item(row,0)) {
                auto s=obs_get_source_by_uuid(item->data(Qt::UserRole).toString().toUtf8().constData());
                if(s) obs_frontend_set_current_scene(s);
                obs_source_release(s);
            }
        });
        auto row=new QHBoxLayout; pairLayout->addLayout(row);
        button(row,"Copy Current Scene to Portrait",[this]{invoke("CherriesCopyMain");});
        button(row,"Edit Portrait Scenes",[this]{reveal(dock("VerticalCanvasDockScenes"));});
        obs_frontend_add_dock_by_id("cherriesScenePairsDock","Scene Pairs",pairWidget);
        pairs=dock("cherriesScenePairsDock");

        auto capture=new QWidget; auto captureLayout=new QVBoxLayout(capture);
        auto records=new QHBoxLayout; captureLayout->addLayout(records);
        auto record=button(records,"Record Landscape",[]{
            if(obs_frontend_recording_active()) obs_frontend_recording_stop(); else obs_frontend_recording_start();
        });
        record->setObjectName("cherriesRecordLandscape");
        button(records,"Record / Stop Portrait",[this]{invoke("RecordButtonClicked");});
        auto replay=new QHBoxLayout; captureLayout->addLayout(replay);
        button(replay,"Start / Stop Portrait Replay",[this]{
            auto c=canvas(); auto b=c ? c->findChild<QPushButton *>("canvasBacktrackEnable") : nullptr;
            if(b) b->click();
        });
        button(replay,"Save Portrait Clip",[this]{
            auto c=canvas(); auto b=c ? c->findChild<QPushButton *>("canvasReplay") : nullptr;
            if(b) b->click();
        });
        auto landscapeReplay=new QHBoxLayout; captureLayout->addLayout(landscapeReplay);
        button(landscapeReplay,"Save Landscape Replay",[]{obs_frontend_replay_buffer_save();});
        button(landscapeReplay,"Start / Stop Landscape Replay",[]{
            if(obs_frontend_replay_buffer_active()) obs_frontend_replay_buffer_stop(); else obs_frontend_replay_buffer_start();
        });
        auto camera=new QHBoxLayout; captureLayout->addLayout(camera);
        button(camera,"Portrait Virtual Camera",[this]{invoke("VirtualCamButtonClicked");});
        button(camera,"Recording / Replay Settings",[this]{invoke("ConfigButtonClicked");});
        auto recordingStatus=new QLabel; captureLayout->addWidget(recordingStatus);
        obs_frontend_add_dock_by_id("cherriesCaptureDock","Capture & Clips",capture);
        clips=dock("cherriesCaptureDock");

        auto menu=main->menuBar()->addMenu("Cherries Studio");
        a=menu->addAction("Restore Mockup A Layout"); connect(a,&QAction::triggered,this,[this]{arrange();});
        a=menu->addAction("About Cherries Studio"); connect(a,&QAction::triggered,this,[this]{
            QMessageBox::about(main,"Cherries Studio","Cherries Studio integrates canvas and streaming tools into OBS.\n\nCanvas and multistream engines derived from Aitum Vertical and Aitum Multistream. Copyright their respective contributors. GPL-2.0.\n\nSource and build instructions: github.com/iBustcherries/Cherries-OBS-browser");
        });
        connect(&refresh,&QTimer::timeout,this,[this,record,recordingStatus]{
            updatePairs();
            record->setText(obs_frontend_recording_active()?"Stop Landscape Recording":"Record Landscape");
            auto c=canvas(); auto rec=c?c->findChild<QPushButton *>("canvasRecord"):nullptr;
            auto replay=c?c->findChild<QPushButton *>("canvasBacktrackEnable"):nullptr;
            recordingStatus->setText(QString("Portrait recording: %1 · Replay: %2")
                .arg(rec&&rec->isChecked()?"Active":"Idle",replay&&replay->isChecked()?"Enabled":"Off"));
        });
        refresh.start(1000); updatePairs();
        auto config=obs_frontend_get_app_config();
        if(!config_get_bool(config,"CherriesStudio","LayoutInstalled")) {
            arrange(); config_set_bool(config,"CherriesStudio","LayoutInstalled",true); config_save_safe(config,"tmp",nullptr);
        }
        blog(LOG_INFO,"[Cherries Studio] Mockup A workspace ready");
    }
};

static QPointer<Studio> studio;
static void frontendEvent(enum obs_frontend_event event, void *)
{
    if(event==OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        auto main=static_cast<QMainWindow *>(obs_frontend_get_main_window());
        QTimer::singleShot(0,main,[main]{if(!studio) studio=new Studio(main);});
    } else if(event==OBS_FRONTEND_EVENT_EXIT) { delete studio; studio=nullptr; }
}
bool obs_module_load(void) { obs_frontend_add_event_callback(frontendEvent,nullptr); return true; }
void obs_module_unload(void) { obs_frontend_remove_event_callback(frontendEvent,nullptr); delete studio; studio=nullptr; }
const char *obs_module_name(void) { return "Cherries Studio"; }
