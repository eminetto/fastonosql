#include "core/unqlite/unqlite_driver.h"

extern "C" {
    #include <unqlite.h>
}

#include "common/sprintf.h"
#include "common/utils.h"
#include "fasto/qt/logger.h"

#include "core/command_logger.h"

#include "core/unqlite/unqlite_config.h"
#include "core/unqlite/unqlite_infos.h"

#define INFO_REQUEST "INFO"
#define GET_KEY_PATTERN_1ARGS_S "GET %s"
#define SET_KEY_PATTERN_2ARGS_SS "PUT %s %s"

#define GET_KEYS_PATTERN_1ARGS_I "KEYS a z %d"
#define DELETE_KEY_PATTERN_1ARGS_S "DEL %s"
#define GET_SERVER_TYPE ""

namespace
{
    std::string getUnqliteError(unqlite* context)
    {
        const char *zErr = NULL;
        int iLen = 0;
        unqlite_config(context, UNQLITE_CONFIG_ERR_LOG, &zErr, &iLen);
        return std::string(zErr, iLen);
    }

    int getDataCallback(const void *pData, unsigned int nDatalen, void *str)
    {
        std::string *out = static_cast<std::string *>(str);
        out->assign((const char*)pData, nDatalen);
        return UNQLITE_OK;
    }
}

namespace fastonosql
{
    namespace
    {
        common::Error createConnection(const unqliteConfig& config, unqlite** context)
        {
            DCHECK(*context == NULL);

            unqlite* lcontext = NULL;
            int st = unqlite_open(&lcontext, config.dbname_.c_str(), config.create_if_missing_ ? UNQLITE_OPEN_CREATE : UNQLITE_OPEN_READWRITE);
            if (st != UNQLITE_OK){
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "Fail open database: %s!", getUnqliteError(lcontext));
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }

            *context = lcontext;

            return common::Error();
        }

        common::Error createConnection(UnqliteConnectionSettings* settings, unqlite** context)
        {
            if(!settings){
                return common::make_error_value("Invalid input argument", common::ErrorValue::E_ERROR);
            }

            unqliteConfig config = settings->info();
            return createConnection(config, context);
        }
    }

    common::Error testConnection(fastonosql::UnqliteConnectionSettings *settings)
    {
        unqlite* ldb = NULL;
        common::Error er = createConnection(settings, &ldb);
        if(er){
            return er;
        }

        unqlite_close(ldb);

        return common::Error();
    }

    struct UnqliteDriver::pimpl
    {
        pimpl()
            : unqlite_(NULL)
        {

        }

        bool isConnected() const
        {
            if(!unqlite_){
                return false;
            }

            return true;
        }

        common::Error connect()
        {
            if(isConnected()){
                return common::Error();
            }

            clear();
            init();

            unqlite* context = NULL;
            common::Error er = createConnection(config_, &context);
            if(er){
                return er;
            }

            unqlite_ = context;


            return common::Error();
        }

        common::Error disconnect()
        {
            if(!isConnected()){
                return common::Error();
            }

            clear();
            return common::Error();
        }

        common::Error info(const char* args, UnqliteServerInfo::Stats& statsout)
        {
            /*std::string rets;
            bool isok = rocksdb_->GetProperty("rocksdb.stats", &rets);
            if (!isok){
                return common::make_error_value("info function failed", common::ErrorValue::E_ERROR);
            }

            if(rets.size() > sizeof(ROCKSDB_HEADER_STATS)){
                const char * retsc = rets.c_str() + sizeof(ROCKSDB_HEADER_STATS);
                char* p2 = strtok((char*)retsc, " ");
                int pos = 0;
                while(p2){
                    switch(pos++){
                        case 0:
                            statsout.compactions_level_ = atoi(p2);
                            break;
                        case 1:
                            statsout.file_size_mb_ = atoi(p2);
                            break;
                        case 2:
                            statsout.time_sec_ = atoi(p2);
                            break;
                        case 3:
                            statsout.read_mb_ = atoi(p2);
                            break;
                        case 4:
                            statsout.write_mb_ = atoi(p2);
                            break;
                        default:
                            break;
                    }
                    p2 = strtok(0, " ");
                }
            }*/

            return common::Error();
        }

        common::Error dbsize(size_t& size) WARN_UNUSED_RESULT
        {
            /* Allocate a new cursor instance */
            unqlite_kv_cursor *pCur; /* Cursor handle */
            int rc = unqlite_kv_cursor_init(unqlite_, &pCur);
            if(rc != UNQLITE_OK){
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "dbsize function error: %s", getUnqliteError(unqlite_));
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }
            /* Point to the first record */
            unqlite_kv_cursor_first_entry(pCur);

            size_t sz = 0;
            /* Iterate over the entries */
            while(unqlite_kv_cursor_valid_entry(pCur)){
                sz++;
                /* Point to the next entry */
                unqlite_kv_cursor_next_entry(pCur);
            }
            /* Finally, Release our cursor */
            unqlite_kv_cursor_release(unqlite_, pCur);

            size = sz;
            return common::Error();
        }

        ~pimpl()
        {
            clear();
        }

        unqliteConfig config_;

        virtual common::Error execute_impl(FastoObject* out, int argc, char **argv)
        {
            if(strcasecmp(argv[0], "info") == 0){
                if(argc > 2){
                    return common::make_error_value("Invalid info input argument", common::ErrorValue::E_ERROR);
                }

                UnqliteServerInfo::Stats statsout;
                common::Error er = info(argc == 2 ? argv[1] : 0, statsout);
                if(!er){
                    common::StringValue *val = common::Value::createStringValue(UnqliteServerInfo(statsout).toString());
                    FastoObject* child = new FastoObject(out, val, config_.mb_delim_);
                    out->addChildren(child);
                }
                return er;
            }
            else if(strcasecmp(argv[0], "get") == 0){
                if(argc != 2){
                    return common::make_error_value("Invalid get input argument", common::ErrorValue::E_ERROR);
                }

                std::string ret;
                common::Error er = get(argv[1], &ret);
                if(!er){
                    common::StringValue *val = common::Value::createStringValue(ret);
                    FastoObject* child = new FastoObject(out, val, config_.mb_delim_);
                    out->addChildren(child);
                }
                return er;
            }
            else if(strcasecmp(argv[0], "dbsize") == 0){
                if(argc != 1){
                    return common::make_error_value("Invalid dbsize input argument", common::ErrorValue::E_ERROR);
                }

                size_t ret = 0;
                common::Error er = dbsize(ret);
                if(!er){
                    common::FundamentalValue *val = common::Value::createUIntegerValue(ret);
                    FastoObject* child = new FastoObject(out, val, config_.mb_delim_);
                    out->addChildren(child);
                }
                return er;
            }
            else if(strcasecmp(argv[0], "put") == 0){
                if(argc != 3){
                    return common::make_error_value("Invalid put input argument", common::ErrorValue::E_ERROR);
                }

                common::Error er = put(argv[1], argv[2]);
                if(!er){
                    common::StringValue *val = common::Value::createStringValue("STORED");
                    FastoObject* child = new FastoObject(out, val, config_.mb_delim_);
                    out->addChildren(child);
                }
                return er;
            }
            else if(strcasecmp(argv[0], "del") == 0){
                if(argc != 2){
                    return common::make_error_value("Invalid del input argument", common::ErrorValue::E_ERROR);
                }

                common::Error er = del(argv[1]);
                if(!er){
                    common::StringValue *val = common::Value::createStringValue("DELETED");
                    FastoObject* child = new FastoObject(out, val, config_.mb_delim_);
                    out->addChildren(child);
                }
                return er;
            }
            else if(strcasecmp(argv[0], "keys") == 0){
                if(argc != 4){
                    return common::make_error_value("Invalid keys input argument", common::ErrorValue::E_ERROR);
                }

                std::vector<std::string> keysout;
                common::Error er = keys(argv[1], argv[2], atoll(argv[3]), &keysout);
                if(!er){
                    common::ArrayValue* ar = common::Value::createArrayValue();
                    for(int i = 0; i < keysout.size(); ++i){
                        common::StringValue *val = common::Value::createStringValue(keysout[i]);
                        ar->append(val);
                    }
                    FastoObjectArray* child = new FastoObjectArray(out, ar, config_.mb_delim_);
                    out->addChildren(child);
                }
                return er;
            }
            else{
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "Not supported command: %s", argv[0]);
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }
        }

    private:
        common::Error get(const std::string& key, std::string* ret_val)
        {
            int rc = unqlite_kv_fetch_callback(unqlite_, key.c_str(), key.size(), getDataCallback, ret_val);
            if (rc != UNQLITE_OK){
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "get function error: %s", getUnqliteError(unqlite_));
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }

            return common::Error();
        }

        common::Error put(const std::string& key, const std::string& value)
        {
            int rc = unqlite_kv_store(unqlite_, key.c_str(), key.size(), value.c_str(), value.length());
            if (rc != UNQLITE_OK){
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "put function error: %s", getUnqliteError(unqlite_));
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }

            return common::Error();
        }

        common::Error del(const std::string& key)
        {
            int rc = unqlite_kv_delete(unqlite_, key.c_str(), key.size());
            if (rc != UNQLITE_OK){
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "delete function error: %s", getUnqliteError(unqlite_));
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }

            return common::Error();
        }

        common::Error keys(const std::string &key_start, const std::string &key_end, uint64_t limit, std::vector<std::string> *ret)
        {
            /* Allocate a new cursor instance */
            unqlite_kv_cursor *pCur; /* Cursor handle */
            int rc = unqlite_kv_cursor_init(unqlite_, &pCur);
            if(rc != UNQLITE_OK){
                char buff[1024] = {0};
                common::SNPrintf(buff, sizeof(buff), "Keys function error: %s", getUnqliteError(unqlite_));
                return common::make_error_value(buff, common::ErrorValue::E_ERROR);
            }
            /* Point to the first record */
            unqlite_kv_cursor_first_entry(pCur);

            /* Iterate over the entries */
            while(unqlite_kv_cursor_valid_entry(pCur) && limit > ret->size()){
                std::string key;
                unqlite_kv_cursor_key_callback(pCur, getDataCallback, &key);
                if(key_start < key && key_end > key){
                    ret->push_back(key);
                }

                /* Point to the next entry */
                unqlite_kv_cursor_next_entry(pCur);
            }
            /* Finally, Release our cursor */
            unqlite_kv_cursor_release(unqlite_, pCur);

            return common::Error();
        }

        void init()
        {

        }

        void clear()
        {
            unqlite_close(unqlite_);
            unqlite_ = NULL;
        }

        unqlite* unqlite_;
    };

    UnqliteDriver::UnqliteDriver(IConnectionSettingsBaseSPtr settings)
        : IDriver(settings, UNQLITE), impl_(new pimpl)
    {

    }

    UnqliteDriver::~UnqliteDriver()
    {
        delete impl_;
    }

    bool UnqliteDriver::isConnected() const
    {
        return impl_->isConnected();
    }

    bool UnqliteDriver::isAuthenticated() const
    {
        return impl_->isConnected();
    }

    // ============== commands =============//
    common::Error UnqliteDriver::commandDeleteImpl(CommandDeleteKey* command, std::string& cmdstring) const
    {
        char patternResult[1024] = {0};
        NDbKValue key = command->key();
        common::SNPrintf(patternResult, sizeof(patternResult), DELETE_KEY_PATTERN_1ARGS_S, key.keyString());
        cmdstring = patternResult;

        return common::Error();
    }

    common::Error UnqliteDriver::commandLoadImpl(CommandLoadKey* command, std::string& cmdstring) const
    {
        char patternResult[1024] = {0};
        NDbKValue key = command->key();
        common::SNPrintf(patternResult, sizeof(patternResult), GET_KEY_PATTERN_1ARGS_S, key.keyString());
        cmdstring = patternResult;

        return common::Error();
    }

    common::Error UnqliteDriver::commandCreateImpl(CommandCreateKey* command, std::string& cmdstring) const
    {
        char patternResult[1024] = {0};
        NDbKValue key = command->key();
        NValue val = command->value();
        common::Value* rval = val.get();
        std::string key_str = key.keyString();
        std::string value_str = common::convertToString(rval, " ");
        common::SNPrintf(patternResult, sizeof(patternResult), SET_KEY_PATTERN_2ARGS_SS, key_str, value_str);
        cmdstring = patternResult;

        return common::Error();
    }

    common::Error UnqliteDriver::commandChangeTTLImpl(CommandChangeTTL* command, std::string& cmdstring) const
    {
        UNUSED(command);
        UNUSED(cmdstring);
        char errorMsg[1024] = {0};
        common::SNPrintf(errorMsg, sizeof(errorMsg), "Sorry, but now " PROJECT_NAME_TITLE " not supported change ttl command for %s.", common::convertToString(connectionType()));
        return common::make_error_value(errorMsg, common::ErrorValue::E_ERROR);
    }

     // ============== commands =============//

    common::net::hostAndPort UnqliteDriver::address() const
    {
        //return common::net::hostAndPort(impl_->config_.hostip_, impl_->config_.hostport_);
        return common::net::hostAndPort();
    }

    std::string UnqliteDriver::outputDelemitr() const
    {
        return impl_->config_.mb_delim_;
    }

    const char* UnqliteDriver::versionApi()
    {
        return UNQLITE_VERSION;
    }

    void UnqliteDriver::initImpl()
    {
    }

    void UnqliteDriver::clearImpl()
    {
    }

    common::Error UnqliteDriver::executeImpl(FastoObject* out, int argc, char **argv)
    {
        return impl_->execute_impl(out, argc, argv);
    }

    common::Error UnqliteDriver::serverInfo(ServerInfo **info)
    {
        LOG_COMMAND(Command(INFO_REQUEST, common::Value::C_INNER));
        UnqliteServerInfo::Stats cm;
        common::Error err = impl_->info(NULL, cm);
        if(!err){
            *info = new UnqliteServerInfo(cm);
        }

        return err;
    }

    common::Error UnqliteDriver::serverDiscoveryInfo(ServerInfo **sinfo, ServerDiscoveryInfo **dinfo, DataBaseInfo** dbinfo)
    {
        ServerInfo *lsinfo = NULL;
        common::Error er = serverInfo(&lsinfo);
        if(er){
            return er;
        }

        FastoObjectIPtr root = FastoObject::createRoot(GET_SERVER_TYPE);
        FastoObjectCommand* cmd = createCommand<UnqliteCommand>(root, GET_SERVER_TYPE, common::Value::C_INNER);
        er = execute(cmd);

        if(!er){
            FastoObject::child_container_type ch = root->childrens();
            if(ch.size()){
                //*dinfo = makeOwnRedisDiscoveryInfo(ch[0]);
            }
        }

        DataBaseInfo* ldbinfo = NULL;
        er = currentDataBaseInfo(&ldbinfo);
        if(er){
            delete lsinfo;
            return er;
        }

        *sinfo = lsinfo;
        *dbinfo = ldbinfo;
        return er;
    }

    common::Error UnqliteDriver::currentDataBaseInfo(DataBaseInfo** info)
    {
        size_t size = 0;
        impl_->dbsize(size);
        *info = new UnqliteDataBaseInfo("0", true, size);
        return common::Error();
    }

    void UnqliteDriver::handleConnectEvent(events::ConnectRequestEvent *ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::ConnectResponceEvent::value_type res(ev->value());
            UnqliteConnectionSettings *set = dynamic_cast<UnqliteConnectionSettings*>(settings_.get());
            if(set){
                impl_->config_ = set->info();
        notifyProgress(sender, 25);
                    common::Error er = impl_->connect();
                    if(er){
                        res.setErrorInfo(er);
                    }
        notifyProgress(sender, 75);
            }
            reply(sender, new events::ConnectResponceEvent(this, res));
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleDisconnectEvent(events::DisconnectRequestEvent* ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::DisconnectResponceEvent::value_type res(ev->value());
        notifyProgress(sender, 50);

            common::Error er = impl_->disconnect();
            if(er){
                res.setErrorInfo(er);
            }

            reply(sender, new events::DisconnectResponceEvent(this, res));
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleExecuteEvent(events::ExecuteRequestEvent* ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::ExecuteRequestEvent::value_type res(ev->value());
            const char *inputLine = common::utils::c_strornull(res.text_);

            common::Error er;
            if(inputLine){
                size_t length = strlen(inputLine);
                int offset = 0;
                RootLocker lock = make_locker(sender, inputLine);
                FastoObjectIPtr outRoot = lock.root_;
                double step = 100.0f/length;
                for(size_t n = 0; n < length; ++n){
                    if(interrupt_){
                        er.reset(new common::ErrorValue("Interrupted exec.", common::ErrorValue::E_INTERRUPTED));
                        res.setErrorInfo(er);
                        break;
                    }
                    if(inputLine[n] == '\n' || n == length-1){
        notifyProgress(sender, step * n);
                        char command[128] = {0};
                        if(n == length-1){
                            strcpy(command, inputLine + offset);
                        }
                        else{
                            strncpy(command, inputLine + offset, n - offset);
                        }
                        offset = n + 1;
                        FastoObjectCommand* cmd = createCommand<UnqliteCommand>(outRoot, stableCommand(command), common::Value::C_USER);
                        er = execute(cmd);
                        if(er){
                            res.setErrorInfo(er);
                            break;
                        }
                    }
                }
            }
            else{
                er.reset(new common::ErrorValue("Empty command line.", common::ErrorValue::E_ERROR));
            }

            if(er){
                LOG_ERROR(er, true);
            }
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleCommandRequestEvent(events::CommandRequestEvent* ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::CommandResponceEvent::value_type res(ev->value());
            std::string cmdtext;
            common::Error er = commandByType(res.cmd_, cmdtext);
            if(er){
                res.setErrorInfo(er);
                reply(sender, new events::CommandResponceEvent(this, res));
                notifyProgress(sender, 100);
                return;
            }

            RootLocker lock = make_locker(sender, cmdtext);
            FastoObjectIPtr root = lock.root_;
            FastoObjectCommand* cmd = createCommand<UnqliteCommand>(root, cmdtext, common::Value::C_INNER);
        notifyProgress(sender, 50);
            er = execute(cmd);
            if(er){
                res.setErrorInfo(er);
            }
            reply(sender, new events::CommandResponceEvent(this, res));
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleLoadDatabaseInfosEvent(events::LoadDatabasesInfoRequestEvent* ev)
    {
        QObject *sender = ev->sender();
    notifyProgress(sender, 0);
        events::LoadDatabasesInfoResponceEvent::value_type res(ev->value());
    notifyProgress(sender, 50);
        res.databases_.push_back(currentDatabaseInfo());
        reply(sender, new events::LoadDatabasesInfoResponceEvent(this, res));
    notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleLoadDatabaseContentEvent(events::LoadDatabaseContentRequestEvent *ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::LoadDatabaseContentResponceEvent::value_type res(ev->value());
            char patternResult[1024] = {0};
            common::SNPrintf(patternResult, sizeof(patternResult), GET_KEYS_PATTERN_1ARGS_I, res.countKeys_);
            FastoObjectIPtr root = FastoObject::createRoot(patternResult);
        notifyProgress(sender, 50);
            FastoObjectCommand* cmd = createCommand<UnqliteCommand>(root, patternResult, common::Value::C_INNER);
            common::Error er = execute(cmd);
            if(er){
                res.setErrorInfo(er);
            }
            else{
                FastoObject::child_container_type rchildrens = cmd->childrens();
                if(rchildrens.size()){
                    DCHECK(rchildrens.size() == 1);
                    FastoObjectArray* array = dynamic_cast<FastoObjectArray*>(rchildrens[0]);
                    if(!array){
                        goto done;
                    }
                    common::ArrayValue* ar = array->array();
                    if(!ar){
                        goto done;
                    }

                    for(int i = 0; i < ar->size(); ++i)
                    {
                        std::string key;
                        bool isok = ar->getString(i, &key);
                        if(isok){
                            NKey k(key);
                            NDbKValue ress(k, NValue());
                            res.keys_.push_back(ress);
                        }
                    }
                }
            }
    done:
        notifyProgress(sender, 75);
            reply(sender, new events::LoadDatabaseContentResponceEvent(this, res));
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleSetDefaultDatabaseEvent(events::SetDefaultDatabaseRequestEvent* ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::SetDefaultDatabaseResponceEvent::value_type res(ev->value());
        notifyProgress(sender, 50);
            reply(sender, new events::SetDefaultDatabaseResponceEvent(this, res));
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleLoadServerInfoEvent(events::ServerInfoRequestEvent* ev)
    {
        QObject *sender = ev->sender();
        notifyProgress(sender, 0);
            events::ServerInfoResponceEvent::value_type res(ev->value());
        notifyProgress(sender, 50);
            LOG_COMMAND(Command(INFO_REQUEST, common::Value::C_INNER));
            UnqliteServerInfo::Stats cm;
            common::Error err = impl_->info(NULL, cm);
            if(err){
                res.setErrorInfo(err);
            }
            else{
                ServerInfoSPtr mem(new UnqliteServerInfo(cm));
                res.setInfo(mem);
            }
        notifyProgress(sender, 75);
            reply(sender, new events::ServerInfoResponceEvent(this, res));
        notifyProgress(sender, 100);
    }

    void UnqliteDriver::handleProcessCommandLineArgs(events::ProcessConfigArgsRequestEvent* ev)
    {

    }

    ServerInfoSPtr UnqliteDriver::makeServerInfoFromString(const std::string& val)
    {
        ServerInfoSPtr res(makeUnqliteServerInfo(val));
        return res;
    }
}
