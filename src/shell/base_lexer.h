#pragma once

#include <Qsci/qsciabstractapis.h>
#include <Qsci/qscilexercustom.h>

#include "core/types.h"

namespace fastonosql
{
    class BaseQsciApi
            : public QsciAbstractAPIs
    {
        Q_OBJECT
    public:
        explicit BaseQsciApi(QsciLexer* lexer);
        void setFilteredVersion(uint32_t version);

    protected:
        bool canSkipCommand(const CommandInfo& info) const;

    private:
        uint32_t filtered_version_;
    };

    class BaseQsciLexer
            : public QsciLexerCustom
    {
        Q_OBJECT
    public:
        enum
        {
            Default = 0,
            Command = 1,
            HelpKeyword
        };

        virtual const char* version() const = 0;
        virtual const char* basedOn() const = 0;

        virtual std::vector<uint32_t> supportedVersions() const = 0;
        virtual uint32_t commandsCount() const = 0;

        virtual QString description(int style) const;
        virtual QColor defaultColor(int style) const;

    protected:
        explicit BaseQsciLexer(QObject* parent = 0);
    };

    QString makeCallTip(const CommandInfo& info);
}
