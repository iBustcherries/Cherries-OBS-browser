// CI-only plugin; loaded into the installed OBS in an isolated temporary profile.
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <QTableWidget>
#include <QFile>
#include <cmath>
#include <cstdlib>

OBS_DECLARE_MODULE()
static QMainWindow *window;
static QTimer *toneTimer;
static obs_source_t *tone;
static obs_source_t *color;
static obs_output_t *mainOutput, *sharedOutput, *portraitOutput;
static void require(bool ok, const char *message) {
    if (!ok) { blog(LOG_ERROR,"[studio-test] FAIL: %s",message); std::_Exit(21); }
    blog(LOG_INFO,"[studio-test] PASS: %s",message);
}
static const char *toneName(void *) { return "Studio test tone"; }
static void *toneCreate(obs_data_t *,obs_source_t *source) { return source; }
static void toneDestroy(void *) {}

static void finish() {
    require(!obs_frontend_streaming_active() && !obs_output_active(sharedOutput) && !obs_output_active(portraitOutput),"all outputs stop");
    obs_output_release(mainOutput); obs_output_release(sharedOutput); obs_output_release(portraitOutput);
    obs_frontend_save();
    window->resize(1600,1000);
    window->grab().save(QString::fromUtf8(getenv("CHERRIES_TEST_ARTIFACTS"))+"/studio-workspace.png");
    QFile file(QString::fromUtf8(getenv("CHERRIES_TEST_ARTIFACTS"))+"/studio-pass.txt");
    require(file.open(QIODevice::WriteOnly),"write test result"); file.write("passed\n"); file.close();
    toneTimer->stop(); obs_source_release(tone); obs_source_release(color);
    blog(LOG_INFO,"[studio-test] Complete: scenes, shared sources, encoder sharing, independent portrait output and stop");
    QTimer::singleShot(500,window,[=]{window->close();});
}

static void verifyStreams() {
    mainOutput=obs_frontend_get_streaming_output();
    sharedOutput=obs_get_output_by_name("aitum_multi_output_Shared Test");
    calldata_t cd; calldata_init(&cd); calldata_set_string(&cd,"name","Portrait Test");
    proc_handler_call(obs_get_proc_handler(),"aitum_vertical_get_stream_output",&cd);
    portraitOutput=static_cast<obs_output_t *>(calldata_ptr(&cd,"output")); calldata_free(&cd);
    require(mainOutput&&sharedOutput&&portraitOutput,"three output instances exist");
    require(obs_output_active(mainOutput)&&obs_output_active(sharedOutput)&&obs_output_active(portraitOutput),"three local RTMP destinations streaming");
    require(obs_output_get_video_encoder(mainOutput)==obs_output_get_video_encoder(sharedOutput),"landscape destinations share one video encoder");
    require(obs_output_get_audio_encoder(mainOutput,0)!=obs_output_get_audio_encoder(sharedOutput,0),"destination audio encoder is independent");
    require(obs_output_get_video_encoder(mainOutput)!=obs_output_get_video_encoder(portraitOutput),"portrait has its own video encoder");
    require(obs_output_get_width(portraitOutput)==360&&obs_output_get_height(portraitOutput)==640,"portrait dimensions are correct");
    require(obs_output_get_total_frames(sharedOutput)>30&&obs_output_get_total_frames(portraitOutput)>30,"both additional destinations receive video frames");
    obs_output_stop(sharedOutput); obs_output_stop(portraitOutput); obs_frontend_streaming_stop();
    QTimer::singleShot(5000,window,finish);
}

static void run() {
    require(window->findChild<QObject *>("cherriesStudioController"),"workspace controller loaded");
    auto dock=window->findChild<QDockWidget *>("VerticalCanvasDock");
    auto dest=window->findChild<QDockWidget *>("AitumMultistreamDock");
    require(dock&&dest&&window->findChild<QTableWidget *>("cherriesScenePairs"),"portrait, destinations and scene-pair controls exist");
    auto sceneSource=obs_frontend_get_current_scene();
    require(sceneSource,"main scene exists");
    auto scene=obs_scene_from_source(sceneSource);
    auto settings=obs_data_create(); obs_data_set_int(settings,"color",0xff405ac4);
    obs_data_set_int(settings,"width",640); obs_data_set_int(settings,"height",360);
    color=obs_source_create("color_source_v3","Shared Test Color",settings,nullptr); obs_data_release(settings);
    require(color,"test video source created"); obs_scene_add(scene,color);
    obs_source_info info={}; info.id="cherries_studio_test_tone"; info.type=OBS_SOURCE_TYPE_INPUT;
    info.output_flags=OBS_SOURCE_AUDIO; info.get_name=toneName; info.create=toneCreate; info.destroy=toneDestroy;
    obs_register_source(&info);
    tone=obs_source_create(info.id,"Track 2 test tone",nullptr,nullptr); require(tone,"test audio source created");
    obs_source_set_audio_mixers(tone,2); obs_scene_add(scene,tone);
    toneTimer=new QTimer(window);
    QObject::connect(toneTimer,&QTimer::timeout,window,[]{
        static float samples[480]; static uint64_t index=0;
        for(int i=0;i<480;++i) samples[i]=0.2f*std::sin(double(index++)*440.0*6.283185307179586/48000.0);
        obs_source_audio audio={}; audio.data[0]=reinterpret_cast<uint8_t *>(samples); audio.data[1]=audio.data[0];
        audio.frames=480; audio.speakers=SPEAKERS_STEREO; audio.samples_per_sec=48000; audio.format=AUDIO_FORMAT_FLOAT_PLANAR;
        audio.timestamp=os_gettime_ns()-10000000; obs_source_output_audio(tone,&audio);
    }); toneTimer->start(10);
    require(QMetaObject::invokeMethod(dock->widget(),"CherriesCopyMain",Qt::DirectConnection),"copy scene command available");
    auto canvas=obs_get_canvas_by_name("Aitum Vertical"); require(canvas,"portrait canvas exists");
    auto copied=obs_canvas_get_scene_by_name(canvas,(QString::fromUtf8(obs_source_get_name(sceneSource))+" Portrait").toUtf8().constData());
    require(copied,"portrait scene copied");
    auto item=obs_scene_find_source(copied,"Shared Test Color");
    require(item&&obs_sceneitem_get_source(item)==color,"copied layout references the original source");
    obs_scene_release(copied); obs_canvas_release(canvas); obs_source_release(sceneSource);
    QTimer::singleShot(2000,window,[dest]{
        auto start=dest->findChild<QPushButton *>("cherriesStartSelected"); require(start,"group start button exists"); start->click();
        QTimer::singleShot(12000,window,verifyStreams);
    });
}
static void event(enum obs_frontend_event e,void *) {
    if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        window=static_cast<QMainWindow *>(obs_frontend_get_main_window());
        QTimer::singleShot(2500,window,run);
    }
}
bool obs_module_load(void) { obs_frontend_add_event_callback(event,nullptr); return true; }
void obs_module_unload(void) { obs_frontend_remove_event_callback(event,nullptr); }
const char *obs_module_name(void) { return "Cherries CI verification"; }
