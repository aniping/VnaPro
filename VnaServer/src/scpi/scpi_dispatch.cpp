/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_constants.h"
#include "scpi/scpi_engine.h"

namespace VnaScpi {
bool ScpiEngine::DispatchCommonCommand(CommandHandler handler, const ParsedCommand &command)
{
    switch (handler) {
        case CommandHandler::IDENTITY:
            DispatchIdentity(command);
            return true;
        case CommandHandler::CLEAR_STATUS:
            DispatchClearStatus(command);
            return true;
        case CommandHandler::RESET:
            DispatchReset(command);
            return true;
        case CommandHandler::OPERATION_COMPLETE:
            DispatchOperationComplete(command);
            return true;
        case CommandHandler::WAIT:
            DispatchWait(command);
            return true;
        case CommandHandler::ENABLE_REGISTER:
            DispatchEnableRegister(command);
            return true;
        case CommandHandler::STATUS_REGISTER:
            DispatchStatusRegister(command);
            return true;
        case CommandHandler::NEXT_ERROR:
            DispatchNextError(command);
            return true;
        case CommandHandler::ERROR_COUNT:
            DispatchErrorCount(command);
            return true;
        default:
            return false;
    }
}

bool ScpiEngine::DispatchConfigurationCommand(CommandHandler handler, const ParsedCommand &command)
{
    switch (handler) {
        case CommandHandler::FREQUENCY:
            DispatchFrequency(command);
            return true;
        case CommandHandler::POINTS:
            DispatchPoints(command);
            return true;
        case CommandHandler::BANDWIDTH:
            DispatchBandwidth(command);
            return true;
        case CommandHandler::POWER:
            DispatchPower(command);
            return true;
        case CommandHandler::SWEEP_TYPE:
            DispatchSweepType(command);
            return true;
        case CommandHandler::SWEEP_TIMING:
            DispatchSweepTiming(command);
            return true;
        default:
            return false;
    }
}

bool ScpiEngine::DispatchMeasurementCommand(CommandHandler handler, const ParsedCommand &command)
{
    switch (handler) {
        case CommandHandler::TRIGGER_SOURCE:
            DispatchTriggerSource(command);
            return true;
        case CommandHandler::TRIGGER_SCOPE:
            DispatchTriggerScope(command);
            return true;
        case CommandHandler::SWEEP_MODE:
            DispatchSweepMode(command);
            return true;
        case CommandHandler::INITIATE:
            DispatchInitiate(command);
            return true;
        case CommandHandler::DEFINE_MEASUREMENT:
            DispatchDefineMeasurement(command);
            return true;
        case CommandHandler::MEASUREMENT_CATALOG:
            DispatchMeasurementCatalog(command);
            return true;
        case CommandHandler::DELETE_MEASUREMENT:
            DispatchDeleteMeasurement(command);
            return true;
        case CommandHandler::DELETE_ALL_MEASUREMENTS:
            DispatchDeleteAllMeasurements(command);
            return true;
        default:
            return false;
    }
}

bool ScpiEngine::DispatchDataCommand(CommandHandler handler, const ParsedCommand &command)
{
    switch (handler) {
        case CommandHandler::DATA_FORMAT:
            DispatchDataFormat(command);
            return true;
        case CommandHandler::BYTE_ORDER:
            DispatchByteOrder(command);
            return true;
        case CommandHandler::STIMULUS_AXIS:
            DispatchStimulusAxis(command);
            return true;
        case CommandHandler::MEASUREMENT_DATA:
            DispatchMeasurementData(command);
            return true;
        case CommandHandler::MULTIPLE_MEASUREMENT_DATA:
            DispatchMultipleMeasurementData(command);
            return true;
        default:
            return false;
    }
}

void ScpiEngine::DispatchCommand(CommandHandler handler, const ParsedCommand &command)
{
    if (!DispatchCommonCommand(handler, command) && !DispatchConfigurationCommand(handler, command) &&
        !DispatchMeasurementCommand(handler, command) && !DispatchDataCommand(handler, command)) {
        RecordCommandError(command, UNDEFINED_HEADER, "Registered handler has no implementation");
    }
}
}  // 命名空间 VnaScpi
