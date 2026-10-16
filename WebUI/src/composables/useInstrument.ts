import { computed, getCurrentScope, onScopeDispose, shallowRef } from 'vue'
import { ApiError, InstrumentApi } from '../api/instrumentApi'
import type { InstrumentGateway } from '../api/instrumentApi'
import type {
  AcceptedOperation,
  AcquiredFrame,
  ChannelCommand,
  FrameResponse,
  FrameCondition,
  InstrumentNotice,
  InstrumentStatus,
  MeasurementCandidate,
  PendingOperation,
  RunAction,
  SweepControlCommand,
  StimulusZoomCommand,
  SweepPatch,
  DisplayCommand,
  TraceDestination,
  TraceCreationOptions,
} from '../api/types'
import type { CalibrationCommand, CalibrationFileFormat } from '../api/calibrationTypes'
import { calibrationActionLabel, calibrationSummary } from '../domain/calibration'
import { channelFrameCondition } from '../domain/channels'

interface InstrumentOptions {
  api?: InstrumentGateway
  pollMs?: number
  retryMs?: number
  renewMs?: number
  now?: () => number
}

const ACTION_LABEL: Record<RunAction, string> = {
  single: 'Single sweep',
  continuous: 'Continuous sweep',
  hold: 'Hold',
  trigger: 'Manual trigger',
}
const INITIAL_NOTICE = '正在连接采集服务…'

function message(error: unknown): string {
  return error instanceof Error ? error.message : '服务请求失败'
}

/** 统一轮询循环负责读取服务端状态；组件只管理未提交草稿与浏览器窗口状态。 */
export function useInstrument(options: InstrumentOptions = {}) {
  const api = options.api ?? new InstrumentApi()
  const now = options.now ?? Date.now
  const status = shallowRef<InstrumentStatus | null>(null)
  const frame = shallowRef<AcquiredFrame | null>(null)
  const channelFrames = shallowRef<Record<number, FrameResponse>>({})
  const frameCurrent = shallowRef(false)
  const frameError = shallowRef('')
  const connected = shallowRef(false)
  const initialized = shallowRef(false)
  const pendingOperation = shallowRef<PendingOperation | null>(null)
  const error = shallowRef<string | null>(null)
  const notice = shallowRef<InstrumentNotice | null>({
    text: INITIAL_NOTICE,
    tone: 'info',
  })
  const controlBusy = shallowRef(false)
  const writeInFlight = shallowRef(false)
  const uncertainWrite = shallowRef(false)
  const renewing = shallowRef(false)

  let active = false
  let generation = 0
  let timer: ReturnType<typeof setTimeout> | undefined
  let polling = false
  let pollAgain = false
  let runningPoll = Promise.resolve()
  let writeEpoch = 0
  let initialClaimAttempted = false
  let lastRenewed = 0
  let resolveOperation: ((value: InstrumentStatus | null) => void) | null = null

  function selectActiveFrame(id = status.value?.activeChannelId ?? 1): void {
    const response = channelFrames.value[id]
    if (!response) {
      if (status.value?.channels) {
        frame.value = null
        frameCurrent.value = false
      }
      return
    }
    frame.value = response.available ? response : null
    frameCurrent.value = response.current
    frameError.value = response.available ? '' : response.reason
  }

  function acceptFrames(response: FrameResponse, id = status.value?.activeChannelId ?? 1, clearUnavailable = false): void {
    const next = { ...channelFrames.value }
    for (const entry of response.channelFrames ?? [response]) {
      const channelId = entry.channelId ?? id
      const previous = next[channelId]
      next[channelId] = !entry.available && previous?.available && !clearUnavailable
        ? { ...previous, current: false }
        : entry
    }
    channelFrames.value = next
    selectActiveFrame(id)
    const unavailable = (response.channelFrames ?? [response]).find((entry) => (entry.channelId ?? id) === id && !entry.available)
    if (unavailable && !unavailable.available) frameError.value = unavailable.reason
  }

  const hasControl = computed(() =>
    Boolean(
      connected.value &&
      renewing.value &&
      status.value?.control.hasControl &&
      !status.value.control.releasing,
    ),
  )
  const operationGateReady = computed(() =>
    Boolean(
      hasControl.value &&
      !writeInFlight.value &&
      !uncertainWrite.value &&
      !pendingOperation.value &&
      !status.value?.pending &&
      status.value,
    ),
  )
  const operationGateOpen = computed(
    () => operationGateReady.value && !controlBusy.value,
  )
  // 续租只暂缓新写入，不撤销已经开始的本地显示手势；真实失权及仪器忙仍立即中止。
  const canContinueDisplayGesture = computed(
    () => operationGateReady.value &&
      !['Faulted', 'Closed', 'Uninitialized'].includes(status.value?.state ?? 'Uninitialized'),
  )
  const canWrite = computed(
    () => canContinueDisplayGesture.value && !controlBusy.value,
  )
  const canConfigure = computed(
    () =>
      canWrite.value &&
      !status.value?.calibration?.collecting &&
      ['Ready', 'Running'].includes(status.value?.state ?? ''),
  )
  const canChangeMeasurements = computed(
    () =>
      canWrite.value &&
      !status.value?.calibration?.collecting &&
      ['Ready', 'Running'].includes(status.value?.state ?? ''),
  )
  const canCalibrate = computed(
    () => canWrite.value && Boolean(status.value?.calibration && api.calibration),
  )
  const canRun = computed(
    () => canWrite.value && !status.value?.calibration?.collecting,
  )
  // 故障后仍可丢弃校准会话；其他校准写动作继续服从健康状态门禁。
  const canCancelCalibration = computed(() => Boolean(
    operationGateOpen.value &&
    status.value?.calibration?.collecting && api.calibration,
  ))

  const frameCondition = computed<FrameCondition>(() => {
    const currentStatus = status.value
    const currentFrame = frame.value
    if (currentStatus?.channels) return channelFrameCondition(currentStatus, currentStatus.activeChannelId ?? 1, currentFrame, connected.value)
    if (!currentFrame) {
      if (currentStatus?.state === 'Faulted')
        return { kind: 'fault', label: 'FAULT', detail: currentStatus.error }
      return {
        kind: 'empty',
        label: 'NO DATA',
        detail: 'Run a single or continuous sweep to acquire measurement data.',
      }
    }
    if (!connected.value)
      return {
        kind: 'disconnected',
        label: 'DISCONNECTED',
        detail: 'Retained frame · connection unavailable',
      }
    if (currentStatus?.state === 'Faulted')
      return {
        kind: 'fault',
        label: 'FAULT',
        detail: 'Retained frame · ' + currentStatus.error,
      }
    if (
      !currentStatus?.activeConfigValid ||
      currentFrame.channelId !== (currentStatus.activeChannelId ?? 1) ||
      currentFrame.revision !== currentStatus.acquisitionRevision ||
      currentFrame.measurementRevision !== currentStatus.revision
    ) {
      return {
        kind: 'stale',
        label: 'PREVIOUS CONFIG',
        detail:
          'Retained frame · waiting for the applied measurement projection',
      }
    }
    if (currentFrame.displayRevision !== currentStatus.display.revision) {
      return {
        kind: 'stale',
        label: 'PREVIOUS DISPLAY',
        detail: 'Retained projection · waiting for the shared display state',
      }
    }
    if (currentStatus.calibration && currentFrame.calibrationRevision !== currentStatus.calibration.revision) {
      return { kind: 'stale', label: 'PREVIOUS CAL', detail: 'Retained projection · waiting for the applied calibration state' }
    }
    if (currentFrame.runId !== currentStatus.runId) {
      return {
        kind: 'acquiring',
        label: 'PREVIOUS RUN',
        detail: 'Retained frame · waiting for current run',
      }
    }
    if (!frameCurrent.value) {
      if (currentStatus.pending || pendingOperation.value) {
        return {
          kind: 'acquiring',
          label: 'ACQUIRING',
          detail: 'Retained frame · operation in progress',
        }
      }
      return {
        kind: 'stale',
        label: 'RETAINED FRAME',
        detail: frameError.value || 'Current data unavailable',
      }
    }
    return { kind: 'current', label: 'CURRENT FRAME', detail: '' }
  })

  function notify(text: string, tone: InstrumentNotice['tone'] = 'info') {
    notice.value = { text, tone }
    if (tone === 'error') error.value = text
  }

  function settleOperation(value: InstrumentStatus | null) {
    resolveOperation?.(value)
    resolveOperation = null
  }

  function schedule(delay: number) {
    if (!active) return
    if (timer !== undefined) clearTimeout(timer)
    timer = undefined
    if (polling) {
      pollAgain = true
      return
    }
    timer = setTimeout(() => {
      timer = undefined
      void refresh()
    }, delay)
  }

  async function claim(automatic = false): Promise<boolean> {
    if (controlBusy.value) return false
    initialClaimAttempted = true
    controlBusy.value = true
    const epoch = generation
    try {
      await api.claim()
      if (epoch !== generation) return false
      lastRenewed = now()
      renewing.value = true
      error.value = null
      if (!automatic) notify('控制租约已取得，等待读回控制状态。', 'success')
      return true
    } catch (failure) {
      if (epoch !== generation) return false
      const conflict =
        failure instanceof ApiError && [403, 409].includes(failure.status)
      notify(
        automatic && conflict
          ? '另一客户端持有控制权，当前只读查看数据。'
          : message(failure),
        conflict ? 'warning' : 'error',
      )
      return false
    } finally {
      if (epoch === generation) {
        controlBusy.value = false
        if (!polling) schedule(0)
      }
    }
  }

  async function release(): Promise<boolean> {
    if (controlBusy.value) return false
    controlBusy.value = true
    // 等待释放结果前先停止续期，防止较早发出的状态请求返回后重新占用控制租约。
    renewing.value = false
    const epoch = generation
    try {
      await api.release()
      if (epoch !== generation) return false
      lastRenewed = 0
      notify('已请求释放控制权；已受理操作会继续完成。', 'success')
      return true
    } catch (failure) {
      if (epoch === generation) notify(message(failure), 'error')
      return false
    } finally {
      if (epoch === generation) {
        controlBusy.value = false
        schedule(0)
      }
    }
  }

  async function observeOperation(epoch: number): Promise<void> {
    const pending = pendingOperation.value
    if (!pending) return
    let queryingOperation = true
    try {
      // 已受理操作的凭据始终属于原令牌，释放或重新获取控制租约后也不转移。
      const result = await api.operation(pending.id, pending.ownerSessionId)
      queryingOperation = false
      if (
        epoch !== generation ||
        pendingOperation.value?.id !== pending.id ||
        result.state === 'pending'
      )
        return
      if (result.operationId !== pending.id)
        throw new ApiError('服务返回了不匹配的操作编号', 0, '', 'protocol')
      if (result.state === 'failed') {
        pendingOperation.value = null
        notify(
          result.result?.message || '设备操作失败，请检查读回状态。',
          'error',
        )
        settleOperation(null)
        return
      }
      pendingOperation.value = { ...pending, readingBack: true }
      const readback = await api.status()
      if (epoch !== generation || pendingOperation.value?.id !== pending.id)
        return
      if (
        (pending.kind === 'display' ||
          (pending.kind === 'calibration' && !readback.calibration?.collecting)) &&
        readback.display.displayedNames.length
      ) {
        // 周期轮询可能已收到旧投影；显示操作须读回同一显示版本后才撤销手势预览。
        const projection = await api.frame()
        if (epoch !== generation || pendingOperation.value?.id !== pending.id)
          return
        if (projection.available) {
          if (projection.displayRevision !== readback.display.revision) return
          if (readback.calibration && projection.calibrationRevision !== readback.calibration.revision) return
          // HOLD 保留旧采集帧也能更新显示；采集新鲜度继续交由 frameCondition 判断。
          acceptFrames(projection, readback.activeChannelId ?? 1)
        } else {
          // 后端明确确认尚无物理帧时，旧窗口缓存不能继续提供标记投影。
          acceptFrames(projection, readback.activeChannelId ?? 1, true)
        }
      }
      status.value = readback
      selectActiveFrame(readback.activeChannelId ?? 1)
      connected.value = true
      pendingOperation.value = null
      error.value = null
      notify(`${pending.label} 完成，已读回设备状态。`, 'success')
      settleOperation(readback)
    } catch (failure) {
      if (epoch !== generation) return
      if (
        queryingOperation &&
        failure instanceof ApiError &&
        failure.status === 404
      ) {
        pendingOperation.value = null
        notify(
          '操作结果已过期；请根据设备读回状态判断结果，未自动重发。',
          'warning',
        )
        settleOperation(null)
      }
      // 暂时无法查询状态时保留操作凭据；HTTP 202 只表示已受理，不表示操作完成。
    }
  }

  async function pollCycle(epoch: number): Promise<void> {
    const wasConnected = connected.value
    const epochAtRead = writeEpoch
    try {
      // 标准工作区使用独立的已确认预览；收集期间保留 DUT 帧，避免反复传输隐藏矩阵。
      const readFrame = !status.value?.calibration?.collecting
      const [statusResult, frameResult] = await Promise.allSettled([
        api.status(),
        readFrame ? api.frame() : Promise.resolve(null),
      ])
      if (epoch !== generation || !active) return
      if (statusResult.status === 'fulfilled') {
        status.value = statusResult.value
        connected.value = true
        if (
          epochAtRead === writeEpoch &&
          !writeInFlight.value &&
          !statusResult.value.pending
        )
          uncertainWrite.value = false
      } else {
        connected.value = false
        if (wasConnected || !initialized.value)
          notify(message(statusResult.reason), 'error')
      }
      if (frameResult.status === 'fulfilled' && frameResult.value) {
        acceptFrames(frameResult.value)
      } else if (frameResult.status === 'rejected') {
        channelFrames.value = Object.fromEntries(Object.entries(channelFrames.value).map(([id, entry]) =>
          [id, entry.available ? { ...entry, current: false } : entry]))
        frameCurrent.value = false
        frameError.value = message(frameResult.reason)
        if (
          frameResult.reason instanceof ApiError &&
          frameResult.reason.status === 0
        )
          connected.value = false
      }
      if (frame.value?.channelId !== (status.value?.activeChannelId ?? 1)) selectActiveFrame()
      const channelIds = status.value?.channels?.map((channel) => channel.channelId)
      if (channelIds && Object.keys(channelFrames.value).some((id) => !channelIds.includes(Number(id))))
        channelFrames.value = Object.fromEntries(Object.entries(channelFrames.value).filter(([id]) => channelIds.includes(Number(id))))
      initialized.value = true
      if (connected.value) {
        await observeOperation(epoch)
        if (epoch !== generation || !active) return
        const control = status.value?.control
        if (!initialClaimAttempted) {
          initialClaimAttempted = true
          if (
            control?.hasControl ||
            (control?.owner === 'none' && !control.releasing)
          )
            await claim(true)
          else notify('控制权已被其他客户端持有；当前只读查看数据。', 'warning')
        } else if (
          renewing.value &&
          control?.hasControl &&
          !control.releasing &&
          !controlBusy.value &&
          now() - lastRenewed >= (options.renewMs ?? 10000)
        ) {
          await claim(true)
        }
        if (epoch !== generation || !active) return
        if (!wasConnected && notice.value?.text === INITIAL_NOTICE) {
          notify(calibrationSummary(status.value?.calibration))
        }
        if (
          !wasConnected &&
          notice.value?.tone === 'error' &&
          !pendingOperation.value
        ) {
          error.value = null
          notify('连接已恢复，显示设备读回状态。', 'success')
        }
      }
    } catch (failure) {
      if (epoch === generation && active) notify(message(failure), 'error')
    }
  }

  function refresh(): Promise<void> {
    if (!active) return Promise.resolve()
    if (polling) {
      pollAgain = true
      return runningPoll
    }
    if (timer !== undefined) clearTimeout(timer)
    timer = undefined
    polling = true
    pollAgain = false
    const epoch = generation
    runningPoll = pollCycle(epoch).finally(() => {
      polling = false
      if (!active) return
      if (epoch !== generation) {
        schedule(0)
        return
      }
      schedule(
        pollAgain
          ? 0
          : connected.value
            ? (options.pollMs ?? 200)
            : (options.retryMs ?? 1000),
      )
    })
    return runningPoll
  }

  async function submit(
    label: string,
    kind: PendingOperation['kind'],
    request: () => Promise<AcceptedOperation>,
    permission?: boolean,
  ): Promise<InstrumentStatus | null> {
    const allowed = permission ?? (
      kind === 'config'
        ? canConfigure.value
        : kind === 'measurement'
          ? canChangeMeasurements.value
          : canWrite.value)
    if (!allowed) {
      const reason = '请取得控制权，并等待当前操作完成。'
      notify(status.value?.control.readOnlyReason || reason, 'warning')
      return null
    }
    writeInFlight.value = true
    ++writeEpoch
    const epoch = generation
    const ownerSessionId = api.sessionId
    try {
      const accepted = await request()
      if (epoch !== generation) return null
      const completion = new Promise<InstrumentStatus | null>((resolve) => {
        resolveOperation = resolve
      })
      pendingOperation.value = {
        id: accepted.operationId,
        label,
        kind,
        ownerSessionId,
        readingBack: false,
      }
      notify(`${label} 已受理，等待设备完成。`)
      return completion
    } catch (failure) {
      if (epoch !== generation) return null
      uncertainWrite.value =
        failure instanceof ApiError &&
        failure.status === 0 &&
        failure.kind !== 'cancelled'
      notify(
        uncertainWrite.value
          ? `${message(failure)}；受理结果暂无法确认，等待读回服务状态，未自动重发。`
          : message(failure),
        'error',
      )
      return null
    } finally {
      if (epoch === generation) {
        writeInFlight.value = false
        schedule(0)
      }
    }
  }

  function start(): void {
    if (active) return
    active = true
    ++generation
    schedule(0)
  }

  function stop(): void {
    if (!active) return
    active = false
    ++generation
    if (timer !== undefined) clearTimeout(timer)
    timer = undefined
    pollAgain = false
    api.abortRequests()
    controlBusy.value = false
    writeInFlight.value = false
    connected.value = false
    // 终止 HTTP 状态查询不会取消设备已受理的操作。
    settleOperation(null)
  }

  if (getCurrentScope()) onScopeDispose(stop)

  return {
    status,
    frame,
    channelFrames,
    connected,
    initialized,
    frameCurrent,
    frameError,
    pendingOperation,
    error,
    notice,
    controlBusy,
    writeInFlight,
    uncertainWrite,
    hasControl,
    canWrite,
    canConfigure,
    canChangeMeasurements,
    canCalibrate,
    canRun,
    canCancelCalibration,
    canEditDisplay: canWrite,
    canContinueDisplayGesture,
    frameCondition,
    start,
    stop,
    refresh,
    claim: () => claim(false),
    release,
    dismissNotice: () => {
      notice.value = null
    },
    configure: (candidate: SweepPatch, expectedRevision: string, channelId = status.value?.activeChannelId) =>
      submit('Apply configuration', 'config', () =>
        api.configure(candidate, expectedRevision, channelId),
      ),
    addMeasurement: (
      candidate: MeasurementCandidate,
      expectedRevision: string,
    ) =>
      submit('Add measurement', 'measurement', () =>
        api.addMeasurement(candidate, expectedRevision),
      ),
    updateMeasurement: (
      candidate: MeasurementCandidate,
      expectedRevision: string,
    ) =>
      submit('Change measurement', 'measurement', () =>
        api.updateMeasurement(candidate, expectedRevision),
      ),
    deleteMeasurement: (name: string, expectedRevision: string) =>
      submit('Delete measurement', 'measurement', () =>
        api.deleteMeasurement(name, expectedRevision),
      ),
    display: (command: DisplayCommand, expectedDisplayRevision?: string) => {
      const revision = expectedDisplayRevision ?? status.value?.display.revision
      return revision
        ? submit('Apply display', 'display', () =>
            api.display(command, revision),
          )
        : Promise.resolve(null)
    },
    cloneMeasurement: (sourceName: string, destination?: TraceDestination) => {
      const snapshot = status.value
      return snapshot
        ? submit('Add trace', 'measurement', () =>
            api.cloneMeasurement(
              sourceName,
              snapshot.revision,
              snapshot.display.revision,
              destination,
            ),
          )
        : Promise.resolve(null)
    },
    createTrace: (
      destination?: TraceCreationOptions,
      expectedRevision?: string,
      expectedDisplayRevision?: string,
    ) => {
      const snapshot = status.value
      return snapshot
        ? submit('Add trace', 'measurement', () =>
            api.createTrace(
              expectedRevision ?? snapshot.revision,
              expectedDisplayRevision ?? snapshot.display.revision,
              destination,
            ),
          )
        : Promise.resolve(null)
    },
    deleteDiagram: (diagram: number, expectedRevision?: string, expectedDisplayRevision?: string) => {
      const snapshot = status.value
      return snapshot
        ? submit('Delete diagram', 'measurement', () =>
            api.deleteDiagram(
              diagram,
              expectedRevision ?? snapshot.revision,
              expectedDisplayRevision ?? snapshot.display.revision,
            ),
          )
        : Promise.resolve(null)
    },
    createAllSParameters: (
      expectedRevision: string,
      expectedDisplayRevision: string,
    ) =>
      submit('All S-Params', 'measurement', () =>
        api.createAllSParameters(expectedRevision, expectedDisplayRevision),
      ),
    run: (action: RunAction) =>
      canRun.value ? submit(ACTION_LABEL[action], 'sweep', () => api.operate(action)) : Promise.resolve(null),
    sweepControl: (command: SweepControlCommand) => {
      if (!canRun.value || !api.sweepControl || !status.value?.config.selectedSweepMode)
        return Promise.resolve(null)
      const label = command.action === 'mode'
        ? `Sweep mode ${command.mode === 'CONT' ? 'Continuous' : 'Single'}`
        : command.action === 'source'
          ? `Trigger source ${command.source === 'IMM' ? 'Free Run' : 'Manual'}`
          : { start: 'Start Sweep', stop: 'Stop Sweep', trigger: 'Manual Trigger', single: 'Single Sweep' }[command.action]
      const channelId = status.value.activeChannelId
      return submit(label, 'sweep', () => api.sweepControl!(command, channelId))
    },
    stimulusZoom: (command: StimulusZoomCommand, expectedRevision: string, expectedDisplayRevision: string) => {
      const zoom = status.value?.stimulusZoom
      if (!canConfigure.value || !api.stimulusZoom ||
          !(command.action === 'out' ? zoom?.canZoomOut : zoom?.available))
        return Promise.resolve(null)
      const channelId = command.action === 'apply'
        ? status.value?.measurements.find((trace) => trace.name === command.traceName)?.channelId
        : status.value?.activeChannelId
      return submit(command.action === 'out' ? 'Zoom Out' : 'Zoom Stimulus', 'config',
        () => api.stimulusZoom!(command, expectedRevision, expectedDisplayRevision, channelId))
    },
    channel: (command: ChannelCommand) => {
      const snapshot = status.value
      return snapshot && api.channel
        ? submit(command.action === 'select' ? 'Select channel' : command.action === 'delete' ? 'Delete channel' : command.action === 'copy' ? 'Copy channel' : 'New channel',
          'measurement', () => api.channel!(command, snapshot.revision, snapshot.display.revision))
        : Promise.resolve(null)
    },
    calibrate: (command: CalibrationCommand, expectedRevision?: string) => {
      const calibration = status.value?.calibration
      const channelId = status.value?.activeChannelId
      const allowed = command.action === 'cancel' ? canCancelCalibration.value : canCalibrate.value
      return allowed && calibration && api.calibration
        ? submit(calibrationActionLabel(command, calibration), 'calibration', () => api.calibration!(command, expectedRevision ?? calibration.revision, channelId), allowed)
        : Promise.resolve(null)
    },
    exportCalibration: (setId: string, format: CalibrationFileFormat = 'binary') => api.exportCalibration
      ? api.exportCalibration(setId, status.value?.activeChannelId, format)
      : Promise.reject(new Error('当前服务不支持校准集导出。')),
    calibrationPreview: (sessionId: string, stepId: string) => api.calibrationPreview
      ? api.calibrationPreview(sessionId, stepId, status.value?.activeChannelId)
      : Promise.reject(new Error('当前服务未提供标准测量预览。')),
  }
}

export type InstrumentModel = ReturnType<typeof useInstrument>
