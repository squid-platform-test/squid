/*
 * Copyright (C) 1996-2025 The Squid Software Foundation and contributors
 *
 * Squid software is distributed under GPLv2+ license and includes
 * contributions from numerous individuals and organizations.
 * Please see the COPYING and CONTRIBUTORS files for details.
 */

#include "squid.h"
#include "AccessLogEntry.h"
#include "acl/FilledChecklist.h"
#include "globals.h"
#include "HttpReply.h"
#include "icap_log.h"
#include "log/CustomLog.h"
#include "log/File.h"
#include "log/Formats.h"
#include "SquidConfig.h"

int IcapLogfileStatus = LOG_DISABLE;

void
icapLogOpen()
{
    CustomLog *log;

    for (log = Config.Log.icaplogs; log; log = log->next) {
        if (log->type == Log::Format::CLF_NONE)
            continue;

        log->logfile = logfileOpen(log->filename, log->bufferSize, log->fatal);

        IcapLogfileStatus = LOG_ENABLE;
    }
}

void
icapLogClose()
{
    CustomLog *log;

    for (log = Config.Log.icaplogs; log; log = log->next) {
        if (log->logfile) {
            logfileClose(log->logfile);
            log->logfile = nullptr;
        }
    }
}

void
icapLogRotate()
{
    for (CustomLog* log = Config.Log.icaplogs; log; log = log->next) {
        if (log->logfile) {
            logfileRotate(log->logfile, Config.Log.rotateNumber);
        }
    }
}

void icapLogLog(AccessLogEntry::Pointer &al)
{
    if (IcapLogfileStatus == LOG_ENABLE) {
        ACLFilledChecklist checklist(nullptr, al->adapted_request);
        checklist.updateAle(al);
        accessLogLogTo(Config.Log.icaplogs, al, &checklist);
    }
}

