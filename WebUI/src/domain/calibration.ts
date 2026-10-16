import type { CalibrationCommand, CalibrationMethod, CalibrationSnapshot, CalibrationStep } from '../api/calibrationTypes'

export function calibrationMethodLabel(method: CalibrationMethod): string {
  return method === 'onePort' ? 'Refl OSM' : method === 'fourPort' ? 'TOSM · 4-Port' : 'TOSM · 2-Port'
}

export function calibrationPorts(method: CalibrationMethod, port1: number, port2: number): string {
  return method === 'onePort' ? `P${port1}` : method === 'fourPort' ? 'P1, P2, P3, P4' : `P${port1}, P${port2}`
}

export function selectedCalibrationSet(calibration?: CalibrationSnapshot) {
  return calibration?.sets.find((entry) => entry.id === calibration.selectedSetId)
}

export function calibrationSummary(calibration?: CalibrationSnapshot): string {
  if (!calibration) return 'RAW 接收波与 S 参数 · 未校准'
  if (!calibration?.selectedSetId) return 'Cal Off · 无活动校准'
  const set = selectedCalibrationSet(calibration)
  const state = calibration.enabled && calibration.compatible ? 'User Cal On' : 'Cal Off'
  return set ? `${state} · ${set.name} · ${calibrationMethodLabel(set.method)} · ${calibrationPorts(set.method, set.port1, set.port2)}` : state
}

export function calibrationConnection(step: CalibrationStep, method?: CalibrationMethod): string {
  const inactive = method === 'fourPort' ? ' 其余未参与连接的端口均接 50 Ω 匹配负载。' : ''
  switch (step.kind) {
    case 'open': return `将 Open 开路标准连接至 P${step.port1}。` + inactive
    case 'short': return `将 Short 短路标准连接至 P${step.port1}。` + inactive
    case 'load': return `将 Match 50 Ω 匹配负载连接至 P${step.port1}。` + inactive
    case 'thru': return `将 Through 直通标准连接于 P${step.port1} 与 P${step.port2} 之间。` + inactive
    case 'isolation': return `分别为 P${step.port1} 与 P${step.port2} 连接 50 Ω 匹配负载，断开两端口直通。` + inactive
  }
}

export const STANDARD_LABELS = { open: 'Open', short: 'Short', load: 'Match', thru: 'Through', isolation: 'Isolation' } as const

export function calibrationActionLabel(command: CalibrationCommand, calibration: CalibrationSnapshot): string {
  switch (command.action) {
    case 'begin': return '创建校准会话'
    case 'capture': {
      const step = calibration.steps.find((entry) => entry.id === command.stepId)
      return step ? `采集标准 ${STANDARD_LABELS[step.kind]}（${step.port2 ? `P${step.port1}, P${step.port2}` : `P${step.port1}`}）` : '采集标准'
    }
    case 'finish': return '应用校准'
    case 'cancel': return '取消校准'
    case 'enable': return command.enabled ? '启用校正' : '停用校正'
    case 'select': return '选择校准集'
    case 'delete': return '删除校准集'
    case 'importFile': return '导入校准集'
  }
}
