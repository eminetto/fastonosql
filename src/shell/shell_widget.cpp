#include "shell/shell_widget.h"

#include <QProgressBar>
#include <QSplitter>
#include <QAction>
#include <QToolBar>
#include <QVBoxLayout>
#include <QApplication>
#include <QFileDialog>
#include <QComboBox>
#include <QLabel>

#include "fasto/qt/logger.h"
#include "common/qt/convert_string.h"
#include "common/sprintf.h"
#include "fasto/qt/gui/icon_label.h"
#include "fasto/qt/utils_qt.h"

#include "core/settings_manager.h"
#include "core/iserver.h"

#include "shell/base_shell.h"
#include "gui/shortcuts.h"

#include "gui/gui_factory.h"

#include "translations/global.h"

using namespace fastonosql::translations;

namespace
{
    const QSize iconSize = QSize(24, 24);
}

namespace fastonosql
{
    BaseShellWidget::BaseShellWidget(IServerSPtr server, const QString& filePath, QWidget* parent)
        : QWidget(parent), server_(server), filePath_(filePath), input_(NULL)
    {
        VERIFY(connect(server_.get(), &IServer::startedConnect, this, &BaseShellWidget::startConnect));
        VERIFY(connect(server_.get(), &IServer::finishedConnect, this, &BaseShellWidget::finishConnect));
        VERIFY(connect(server_.get(), &IServer::startedDisconnect, this, &BaseShellWidget::startDisconnect));
        VERIFY(connect(server_.get(), &IServer::finishedDisconnect, this, &BaseShellWidget::finishDisconnect));
        VERIFY(connect(server_.get(), &IServer::startedExecute, this, &BaseShellWidget::startedExecute));
        VERIFY(connect(server_.get(), &IServer::progressChanged, this, &BaseShellWidget::progressChange));

        VERIFY(connect(server_.get(), &IServer::startedSetDefaultDatabase, this, &BaseShellWidget::startSetDefaultDatabase));
        VERIFY(connect(server_.get(), &IServer::finishedSetDefaultDatabase, this, &BaseShellWidget::finishSetDefaultDatabase));

        VERIFY(connect(server_.get(), &IServer::enteredMode, this, &BaseShellWidget::enterMode));
        VERIFY(connect(server_.get(), &IServer::leavedMode, this, &BaseShellWidget::leaveMode));

        VERIFY(connect(server_.get(), &IServer::rootCreated, this, &BaseShellWidget::rootCreated));
        VERIFY(connect(server_.get(), &IServer::rootCompleated, this, &BaseShellWidget::rootCompleated));

        VERIFY(connect(server_.get(), &IServer::startedLoadDiscoveryInfo, this, &BaseShellWidget::startLoadDiscoveryInfo));
        VERIFY(connect(server_.get(), &IServer::finishedLoadDiscoveryInfo, this, &BaseShellWidget::finishLoadDiscoveryInfo));

        VERIFY(connect(server_.get(), &IServer::addedChild, this, &BaseShellWidget::addedChild));
        VERIFY(connect(server_.get(), &IServer::itemUpdated, this, &BaseShellWidget::itemUpdated, Qt::UniqueConnection));

        QVBoxLayout* mainlayout = new QVBoxLayout;
        QHBoxLayout* hlayout = new QHBoxLayout;

        QToolBar *savebar = new QToolBar;
        savebar->setStyleSheet("QToolBar { border: 0px; }");

        loadAction_ = new QAction(GuiFactory::instance().loadIcon(), trLoad, savebar);
        typedef void (BaseShellWidget::*lf)();
        VERIFY(connect(loadAction_, &QAction::triggered, this, static_cast<lf>(&BaseShellWidget::loadFromFile)));
        savebar->addAction(loadAction_);

        saveAction_ = new QAction(GuiFactory::instance().saveIcon(), trSave, savebar);
        VERIFY(connect(saveAction_, &QAction::triggered, this, &BaseShellWidget::saveToFile));
        savebar->addAction(saveAction_);

        saveAsAction_ = new QAction(GuiFactory::instance().saveAsIcon(), trSaveAs, savebar);
        VERIFY(connect(saveAsAction_, &QAction::triggered, this, &BaseShellWidget::saveToFileAs));
        savebar->addAction(saveAsAction_);

        connectAction_ = new QAction(GuiFactory::instance().connectIcon(), trConnect, savebar);
        VERIFY(connect(connectAction_, &QAction::triggered, this, &BaseShellWidget::connectToServer));
        savebar->addAction(connectAction_);

        disConnectAction_ = new QAction(GuiFactory::instance().disConnectIcon(), trDisconnect, savebar);
        VERIFY(connect(disConnectAction_, &QAction::triggered, this, &BaseShellWidget::disconnectFromServer));
        savebar->addAction(disConnectAction_);

        executeAction_ = new QAction(GuiFactory::instance().executeIcon(), trExecute, savebar);
        executeAction_->setShortcut(executeKey);
        VERIFY(connect(executeAction_, &QAction::triggered, this, &BaseShellWidget::execute));
        savebar->addAction(executeAction_);

        QAction *stopAction = new QAction(GuiFactory::instance().stopIcon(), trStop, savebar);
        VERIFY(connect(stopAction, &QAction::triggered, this, &BaseShellWidget::stop));
        savebar->addAction(stopAction);

        const ConnectionMode mode = InteractiveMode;
        connectionMode_ = new fasto::qt::gui::IconLabel(GuiFactory::instance().modeIcon(mode), common::convertFromString<QString>(common::convertToString(mode)), iconSize);

        dbName_ = new fasto::qt::gui::IconLabel(GuiFactory::instance().databaseIcon(), "Calculate...", iconSize);

        hlayout->addWidget(savebar);

        QSplitter *splitter = new QSplitter;
        splitter->setOrientation(Qt::Horizontal);
        splitter->setHandleWidth(1);
        hlayout->addWidget(splitter);

        hlayout->addWidget(dbName_);
        hlayout->addWidget(connectionMode_);
        workProgressBar_ = new QProgressBar;
        workProgressBar_->setTextVisible(true);
        hlayout->addWidget(workProgressBar_);

        initShellByType(server->type());

        mainlayout->addLayout(hlayout);
        mainlayout->addWidget(input_);

        QHBoxLayout* apilayout = new QHBoxLayout;

        apilayout->addWidget(new QLabel(tr("Supported commands count: %1").arg(input_->commandsCount())));

        QSplitter *splitterButtom = new QSplitter;
        splitterButtom->setOrientation(Qt::Horizontal);
        splitterButtom->setHandleWidth(1);
        apilayout->addWidget(splitterButtom);

        commandsVersionApi_ = new QComboBox;
        typedef void (QComboBox::*curc)(int);
        VERIFY(connect(commandsVersionApi_, static_cast<curc>(&QComboBox::currentIndexChanged), this, &BaseShellWidget::changeVersionApi));

        std::vector<uint32_t> versions = input_->supportedVersions();
        for(int i = 0; i < versions.size(); ++i){
            uint32_t cur = versions[i];
            std::string curVers = convertVersionNumberToReadableString(cur);
            commandsVersionApi_->addItem(GuiFactory::instance().unknownIcon(), common::convertFromString<QString>(curVers), cur);
            commandsVersionApi_->setCurrentIndex(i);
        }
        apilayout->addWidget(new QLabel(tr("Command version:")));
        apilayout->addWidget(commandsVersionApi_);
        mainlayout->addLayout(apilayout);

        setLayout(mainlayout);

        syncConnectionActions();
        syncServerInfo(server_->serverInfo());
        updateDefaultDatabase(server_->currentDatabaseInfo());
    }

    void BaseShellWidget::syncServerInfo(ServerInfoSPtr inf)
    {
        if(!inf){
            return;
        }

        uint32_t servVers = inf->version();
        if(servVers == UNDEFINED_SINCE){
            return;
        }

        bool updatedComboIndex = false;
        for(int i = 0; i < commandsVersionApi_->count(); ++i){
            QVariant var = commandsVersionApi_->itemData(i);
            uint32_t version = qvariant_cast<uint32_t>(var);
            if(version == UNDEFINED_SINCE){
                commandsVersionApi_->setItemIcon(i, GuiFactory::instance().unknownIcon());
                continue;
            }

            if(version >= servVers){
                if(!updatedComboIndex){
                    updatedComboIndex = true;
                    commandsVersionApi_->setCurrentIndex(i);
                    commandsVersionApi_->setItemIcon(i, GuiFactory::instance().successIcon());
                }
                else{
                    commandsVersionApi_->setItemIcon(i, GuiFactory::instance().failIcon());
                }
            }
            else{
                commandsVersionApi_->setItemIcon(i, GuiFactory::instance().successIcon());
            }
        }
    }

    void BaseShellWidget::initShellByType(connectionTypes type)
    {
        input_ = BaseShell::createFromType(type, SettingsManager::instance().autoCompletion());
        DCHECK(input_);
        if(input_){
            setToolTip(tr("Based on %1 version: %2").arg(input_->basedOn()).arg(input_->version()));
            input_->setContextMenuPolicy(Qt::CustomContextMenu);
        }
    }

    BaseShellWidget::~BaseShellWidget()
    {

    }

    QString BaseShellWidget::text() const
    {
        return input_->text();
    }

    IServerSPtr BaseShellWidget::server() const
    {
        return server_;
    }

    void BaseShellWidget::setText(const QString& text)
    {
        input_->setText(text);
    }

    void BaseShellWidget::executeText(const QString& text)
    {
        input_->setText(text);
        execute();
    }

    void BaseShellWidget::execute()
    {
        QString selected = input_->selectedText();
        if(selected.isEmpty()){
            selected = input_->text();
        }

        EventsInfo::ExecuteInfoRequest req(this, common::convertToString(selected));
        server_->execute(req);
    }

    void BaseShellWidget::stop()
    {
        server_->stopCurrentEvent();
    }

    void BaseShellWidget::connectToServer()
    {
        EventsInfo::ConnectInfoRequest req(this);
        server_->connect(req);
    }

    void BaseShellWidget::disconnectFromServer()
    {
        EventsInfo::DisConnectInfoRequest req(this);
        server_->disconnect(req);
    }

    void BaseShellWidget::loadFromFile()
    {
        loadFromFile(filePath_);
    }

    bool BaseShellWidget::loadFromFile(const QString& path)
    {
        bool res = false;
        QString filepath = QFileDialog::getOpenFileName(this, path, QString(), trfilterForScripts);
        if (!filepath.isEmpty()) {
            QString out;
            if (common::utils_qt::loadFromFileText(filepath, out, this)) {
                setText(out);
                filePath_ = filepath;
                res = true;
            }
        }
        return res;
    }

    void BaseShellWidget::saveToFileAs()
    {
        QString filepath = QFileDialog::getSaveFileName(this, trSaveAs, filePath_, trfilterForScripts);

        if (common::utils_qt::saveToFileText(filepath,text(), this)) {
            filePath_ = filepath;
        }
    }

    void BaseShellWidget::changeVersionApi(int index)
    {
        if(index == -1){
            return;
        }

        QVariant var = commandsVersionApi_->itemData(index);
        uint32_t version = qvariant_cast<uint32_t>(var);
        input_->setFilteredVersion(version);
    }

    void BaseShellWidget::saveToFile()
    {
        if(filePath_.isEmpty()){
            saveToFileAs();
        }
        else {
            common::utils_qt::saveToFileText(filePath_, text(), this);
        }
    }

    void BaseShellWidget::startConnect(const EventsInfo::ConnectInfoRequest& req)
    {
        syncConnectionActions();
    }

    void BaseShellWidget::finishConnect(const EventsInfo::ConnectInfoResponce& res)
    {
        syncConnectionActions();
    }

    void BaseShellWidget::startDisconnect(const EventsInfo::DisConnectInfoRequest& req)
    {
        syncConnectionActions();
    }

    void BaseShellWidget::finishDisconnect(const EventsInfo::DisConnectInfoResponce& res)
    {
        syncConnectionActions();
    }

    void BaseShellWidget::startSetDefaultDatabase(const EventsInfo::SetDefaultDatabaseRequest& req)
    {

    }

    void BaseShellWidget::finishSetDefaultDatabase(const EventsInfo::SetDefaultDatabaseResponce& res)
    {
        common::Error er = res.errorInfo();
        if(er && er->isError()){
            return;
        }

        IServer *serv = qobject_cast<IServer *>(sender());
        DCHECK(serv);
        if(!serv){
            return;
        }

        DataBaseInfoSPtr db = res.inf_;
        updateDefaultDatabase(db);
    }

    void BaseShellWidget::progressChange(const EventsInfo::ProgressInfoResponce& res)
    {
        workProgressBar_->setValue(res.progress_);
    }

    void BaseShellWidget::enterMode(const EventsInfo::EnterModeInfo& res)
    {
        ConnectionMode mode = res.mode_;
        connectionMode_->setIcon(GuiFactory::instance().modeIcon(mode), iconSize);
        std::string modeText = common::convertToString(mode);
        connectionMode_->setText(common::convertFromString<QString>(modeText));
    }

    void BaseShellWidget::leaveMode(const EventsInfo::LeaveModeInfo& res)
    {

    }

    void BaseShellWidget::startLoadDiscoveryInfo(const EventsInfo::DiscoveryInfoRequest& res)
    {

    }

    void BaseShellWidget::finishLoadDiscoveryInfo(const EventsInfo::DiscoveryInfoResponce& res)
    {
        common::Error er = res.errorInfo();
        if(er && er->isError()){
            return;
        }

        syncServerInfo(res.sinfo_);
        updateDefaultDatabase(res.dbinfo_);
    }

    void BaseShellWidget::updateDefaultDatabase(DataBaseInfoSPtr dbs)
    {
        if(dbs){
            std::string name = dbs->name();
            dbName_->setText(common::convertFromString<QString>(name));
        }
    }

    void BaseShellWidget::syncConnectionActions()
    {
        connectAction_->setVisible(!server_->isConnected());
        disConnectAction_->setVisible(server_->isConnected());
        executeAction_->setEnabled(server_->isConnected());
    }
}
