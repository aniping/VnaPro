import type { CalibrationApplicability } from './calibrationTypes'
import { array, choice, flag, integer, invalid, number, object, text } from './decodeValues'

/** 缺少新字段的旧服务由调用方处理；提供新协议时必须完整、有限且自洽。 */
export function decodeCalibrationApplicability(value: unknown): CalibrationApplicability {
  const item = object(value, 'calibration.applicability')
  const field = (name: string) => `calibration.applicability.${name}`
  const result: CalibrationApplicability = {
    applicable: flag(item.applicable, field('applicable')),
    prepared: flag(item.prepared, field('prepared')),
    frequencyMode: choice(item.frequencyMode, field('frequencyMode'), ['none', 'exact', 'subset', 'interpolated']),
    reasonCode: choice(item.reasonCode, field('reasonCode'), ['none', 'noSelection', 'configurationNotReady', 'deviceMismatch', 'invalidAxis', 'outOfRange', 'powerMismatch', 'invalidCoefficients']),
    reason: text(item.reason, field('reason')),
    sourcePoints: integer(item.sourcePoints, field('sourcePoints'), 0, 10001),
    targetPoints: integer(item.targetPoints, field('targetPoints'), 0, 10001),
    exactPoints: integer(item.exactPoints, field('exactPoints'), 0, 10001),
    interpolatedPoints: integer(item.interpolatedPoints, field('interpolatedPoints'), 0, 10001),
    targetStartHz: number(item.targetStartHz, field('targetStartHz')),
    targetStopHz: number(item.targetStopHz, field('targetStopHz')),
    targetIfBandwidthHz: number(item.targetIfBandwidthHz, field('targetIfBandwidthHz'), 0),
    targetMinPowerDbm: number(item.targetMinPowerDbm, field('targetMinPowerDbm')),
    targetMaxPowerDbm: number(item.targetMaxPowerDbm, field('targetMaxPowerDbm')),
    warnings: array(item.warnings, field('warnings'), 2).map((entry) =>
      choice(entry, field('warning'), ['ifbwChanged', 'interpolation'])),
  }
  if (new Set(result.warnings).size !== result.warnings.length ||
      result.exactPoints + result.interpolatedPoints > result.targetPoints ||
      result.targetMinPowerDbm > result.targetMaxPowerDbm ||
      (result.prepared && !result.applicable)) invalid('calibration.applicability consistency')
  if (result.applicable && (result.reasonCode !== 'none' || result.frequencyMode === 'none' ||
      result.sourcePoints < 2 || result.targetPoints < 2 || result.targetStartHz <= 0 ||
      result.targetStartHz >= result.targetStopHz ||
      result.targetIfBandwidthHz <= 0 || result.exactPoints + result.interpolatedPoints !== result.targetPoints))
    invalid('calibration.applicability applicable mapping')
  if ((result.frequencyMode === 'exact' || result.frequencyMode === 'subset') && result.interpolatedPoints !== 0)
    invalid('calibration.applicability exact mapping')
  if (result.frequencyMode === 'interpolated' && result.interpolatedPoints === 0)
    invalid('calibration.applicability interpolation')
  return result
}
