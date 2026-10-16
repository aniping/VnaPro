<script setup lang="ts">
import { computed } from 'vue'
import type { CalibrationSetSummary } from '../../api/calibrationTypes'
import type { SweepConfiguration } from '../../api/types'
import { applicabilityMapping, applicabilityRemedy, applicabilityWarnings } from '../../domain/calibrationApplicability'
import { frequencyText } from '../../plot/formatting'

const props = defineProps<{ set: CalibrationSetSummary; config?: SweepConfiguration }>()
const applicability = computed(() => props.set.applicability)
const warnings = computed(() => applicabilityWarnings(applicability.value))
const current = computed(() => applicability.value ? {
  startHz: applicability.value.targetStartHz,
  stopHz: applicability.value.targetStopHz,
  points: applicability.value.targetPoints,
  ifBandwidthHz: applicability.value.targetIfBandwidthHz,
  powerMinDbm: applicability.value.targetMinPowerDbm,
  powerMaxDbm: applicability.value.targetMaxPowerDbm,
} : props.config ? { ...props.config, powerMinDbm: props.config.powerDbm, powerMaxDbm: props.config.powerDbm } : undefined)
function range(start?: number, stop?: number): string {
  return start === undefined || stop === undefined || start <= 0 || stop <= 0 ? '未就绪' : `${frequencyText(start)}–${frequencyText(stop)}`
}
function bandwidth(value?: number): string { return value ? frequencyText(value) : '未记录' }
function power(minimum?: number, maximum?: number): string {
  if (minimum === undefined || maximum === undefined) return '未记录'
  return minimum === maximum ? `${minimum} dBm` : `${minimum}–${maximum} dBm（逐点）`
}
</script>

<template>
  <div class="applicability-details" data-testid="calibration-applicability-details">
    <table class="condition-table" aria-label="校准条件与当前扫频条件对照">
      <thead><tr><th scope="col">条件</th><th scope="col">校准时</th><th scope="col">当前扫频</th></tr></thead>
      <tbody>
        <tr><th scope="row">频率范围</th><td>{{ set.startHz === undefined || set.stopHz === undefined ? '未记录' : range(set.startHz, set.stopHz) }}</td><td>{{ range(current?.startHz, current?.stopHz) }}</td></tr>
        <tr><th scope="row">激励点数</th><td>{{ set.points ?? '未记录' }}</td><td>{{ current?.points || '未就绪' }}</td></tr>
        <tr><th scope="row">IFBW</th><td>{{ bandwidth(set.ifBandwidthHz) }}</td><td>{{ bandwidth(current?.ifBandwidthHz) }}</td></tr>
        <tr><th scope="row">激励功率</th><td>{{ power(set.powerMinDbm, set.powerMaxDbm) }}</td><td>{{ power(current?.powerMinDbm, current?.powerMaxDbm) }}</td></tr>
      </tbody>
    </table>
    <p v-if="set.deviceIdentity" class="device-identity">校准设备：{{ set.deviceIdentity }}</p>
    <template v-if="applicability">
      <p v-if="!applicability.applicable" class="notice warning" data-testid="calibration-applicability-blocked">{{ applicability.reason || '当前扫频条件不适用此校准集。' }} {{ applicabilityRemedy(applicability) }}</p>
      <template v-else>
        <p class="notice mapping" data-testid="calibration-frequency-mapping">{{ applicabilityMapping(applicability) }}</p>
        <p class="notice preparation" data-testid="calibration-preparation">{{ applicability.prepared ? '当前激励点的校准系数已准备完成。' : '适用性预检通过；应用时才准备并校验当前激励点的校准系数。' }}</p>
      </template>
      <p v-for="warning in warnings" :key="warning" class="notice warning" data-testid="calibration-condition-warning">{{ warning }}</p>
    </template>
    <p v-else-if="!set.compatible" class="notice warning">此集与当前通道配置不匹配，不能应用。</p>
    <p v-else class="notice">当前服务未提供详细适用性预检；以应用后的设备读回状态为准。</p>
  </div>
</template>

<style scoped>
.applicability-details { margin-top: 10px; }
.condition-table { width: 100%; border-collapse: collapse; table-layout: fixed; text-align: left; font-size: 11px; font-variant-numeric: tabular-nums; }
.condition-table th, .condition-table td { padding: 4px 8px; border-bottom: 1px solid #35454e; overflow-wrap: anywhere; }
.condition-table th { color: var(--color-muted); font-weight: 500; }
.condition-table th:first-child { width: 82px; }
.condition-table thead { background: var(--color-panel-dark); }
.notice, .device-identity { margin: 7px 0 0; font-size: 11px; line-height: 1.6; overflow-wrap: anywhere; }
.device-identity, .preparation { color: var(--color-muted); }
.warning { color: var(--color-warning); }
</style>
