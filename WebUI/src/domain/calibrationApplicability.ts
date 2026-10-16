import type { CalibrationApplicability, CalibrationSetSummary, CalibrationSnapshot } from '../api/calibrationTypes'

export function calibrationEffective(calibration?: CalibrationSnapshot): boolean {
  return calibration?.effective ?? Boolean(calibration?.enabled && calibration.compatible)
}

export function calibrationCanApply(set?: CalibrationSetSummary): boolean {
  return Boolean(set?.compatible && (set.applicability?.applicable ?? true))
}

export function applicabilityLabel(applicability: CalibrationApplicability | undefined, compatible: boolean): string {
  if (!applicability) return compatible ? '适用' : '不适用'
  if (!applicability.applicable) return applicability.reasonCode === 'outOfRange' ? '频率越界' : '不适用'
  switch (applicability.frequencyMode) {
    case 'exact': return '原校准点'
    case 'subset': return '精确子集'
    case 'interpolated': return applicability.prepared ? '插值校正' : '需插值'
    default: return '待确认'
  }
}

export function calibrationStateLabel(calibration?: CalibrationSnapshot): string {
  if (!calibration?.selectedSetId) return 'Cal Off · 无活动校准'
  if (!calibrationEffective(calibration)) return calibration.enabled ? 'Cal Off · 校准不适用' : 'Cal Off · 校正已关闭'
  switch (calibration.applicability?.frequencyMode) {
    case 'exact': return 'User Cal On · 原校准点'
    case 'subset': return 'User Cal On · 精确子集'
    case 'interpolated': return 'User Cal On · 插值校正'
    default: return 'User Cal On'
  }
}

export function calibrationTone(calibration?: CalibrationSnapshot): 'neutral' | 'success' | 'warning' {
  if (calibration?.applicability?.warnings.length || (calibration?.enabled && !calibrationEffective(calibration))) return 'warning'
  return calibrationEffective(calibration) ? 'success' : 'neutral'
}

export function applicabilityWarnings(applicability?: CalibrationApplicability): string[] {
  return (applicability?.warnings ?? []).map((warning) => warning === 'ifbwChanged'
    ? 'IFBW 与校准时不同，测量噪声可能变化；更窄的测量带宽不能消除校准时的噪声。'
    : '使用误差系数幅度与相位的线性插值；不能保证与原校准点相同的校正精度，建议按目标扫频重新校准。')
}

export function applicabilityMapping(applicability?: CalibrationApplicability): string {
  if (!applicability?.applicable) return ''
  if (applicability.frequencyMode === 'subset') return `${applicability.exactPoints} 个激励点均来自原校准点，直接取值，无插值。`
  if (applicability.frequencyMode === 'exact') return `${applicability.exactPoints} 个激励点与原校准点一致，无插值。`
  return `${applicability.exactPoints} 个点直接取值，${applicability.interpolatedPoints} 个点在校准频段内插值。`
}

export function applicabilityRemedy(applicability?: CalibrationApplicability): string {
  switch (applicability?.reasonCode) {
    case 'outOfRange': return '请将全部激励点收回校准频段内，或在扩大后的频段重新校准；不进行外推或局部校正。'
    case 'powerMismatch': return '请恢复与校准时一致的激励功率，或按当前功率重新校准。'
    case 'deviceMismatch': return '请选择当前设备和端口适用的校准集，或重新校准。'
    case 'invalidAxis': return '请检查扫频频率轴，恢复有效且严格递增的激励点。'
    case 'invalidCoefficients': return '误差系数无法用于当前激励点；请更换校准集或重新校准。'
    case 'configurationNotReady': return '请等待仪器配置完成后再应用校准集。'
    default: return ''
  }
}
