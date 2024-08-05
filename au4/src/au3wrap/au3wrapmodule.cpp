/*
* Audacity: A Digital Audio Editor
*/
#include "au3wrapmodule.h"

#include <wx/log.h>

#include "libraries/lib-preferences/Prefs.h"
#include "libraries/lib-audio-io/AudioIO.h"
#include "libraries/lib-project-file-io/ProjectFileIO.h"

#include "mocks/qtBasicUI.h"

#include "modularity/ioc.h"

#include "internal/wxlogwrap.h"
#include "internal/au3project.h"
#include "internal/trackeditinteraction.h"
#include "internal/au3wavepainter.h"
#include "internal/au3playback.h"
#include "internal/au3record.h"
#include "internal/au3audiodevicesprovider.h"
#include "internal/au3selectioncontroller.h"
#include "internal/au3commonsettings.h"

#include "log.h"

using namespace au::au3;
using namespace muse::modularity;

std::string Au3WrapModule::moduleName() const
{
    return "au3wrap";
}

void Au3WrapModule::registerExports()
{
    m_playback = std::make_shared<Au3Playback>();
    m_record = std::make_shared<Au3Record>();

    m_audioDevicesProvider = std::make_shared<Au3AudioDevicesProvider>();

    ioc()->registerExport<IAu3ProjectCreator>(moduleName(), new Au3ProjectCreator());
    ioc()->registerExport<playback::IPlayback>(moduleName(), m_playback);
    ioc()->registerExport<IAu3Record>(moduleName(), m_record);
    ioc()->registerExport<trackedit::ITrackeditInteraction>(moduleName(), new TrackeditInteraction());
    ioc()->registerExport<IAu3WavePainter>(moduleName(), new Au3WavePainter());
    ioc()->registerExport<trackedit::ISelectionController>(moduleName(), new Au3SelectionController());
    ioc()->registerExport<playback::IAudioDevicesProvider>(moduleName(), m_audioDevicesProvider);
}

void Au3WrapModule::onInit(const muse::IApplication::RunMode&)
{
    m_wxLog = new WxLogWrap();
    wxLog::SetActiveTarget(m_wxLog);

    std::unique_ptr<Au3CommonSettings> auset = std::make_unique<Au3CommonSettings>();
    InitPreferences(std::move(auset));

    AudioIO::Init();

    bool ok = ProjectFileIO::InitializeSQL();
    if (!ok) {
        LOGE() << "failed init sql";
    }

    m_record->init();

    m_audioDevicesProvider->init();

    static QtBasicUI uiServices;
    (void)BasicUI::Install(&uiServices);
}

void Au3WrapModule::onDeinit()
{
    wxLog::SetActiveTarget(nullptr);
    delete m_wxLog;
}
