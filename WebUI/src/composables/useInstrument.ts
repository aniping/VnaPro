import { computed, getCurrentScope, onScopeDispose, shallowRef } from 'vue'
import { ApiError, InstrumentApi } from '../api/instrumentApi'
import type { InstrumentGateway } from '../api/instrumentApi'
import type {
  AcceptedOperation, AcquiredFrame, FrameCondition, InstrumentNotice, InstrumentStatus,
  MeasurementCandidate, PendingOperation, RunAction, SweepConfiguration,
} from '../api/types'

interface InstrumentOptions {
  api?: InstrumentGateway
  pollMs?: number
  retryMs?: number
  renewMs?: number
  now?: () => number
}

const ACTION_LABEL: Record<RunAction, string> = {
  single: 'Single sweep', continuous: 'Continuous sweep', hold: 'Hold', trigger: 'Manual trigger',
}
const INITIAL_NOTICE = '正在连接采集服务…'

function message(error: unknown): string {
  return error instanceof Error ? error.message : '服务请求失败'
}

/** 统一轮询循环负责读取服务端状态；组件只管理本地编辑草稿和显示设置。 */
export function useInstrument(options: InstrumentOptions = {}) {
  const api = options.api ?? new InstrumentApi()
  const now = options.now ?? Date.now
  const status = shallowRef<InstrumentStatus | null>(null)
  const frame = shallowRef<AcquiredFrame | null>(null)
  const frameCurrent = shallowRef(false)
  const frameError = shallowRef('')
  const connected = shallowRef(false)
  const initialized = shallowRef(false)
  const pendingOperation = shallowRef<PendingOperation | null>(null)
  const error = shallowRef<string | null>(null)
  const notice = shallowRef<InstrumentNotice | null>({ text: INITIAL_NOTICE, tone: 'info' })
  const controlBusy = shallowRef(false)
  const writeInFlight = shallowRef(false)
  const uncertainWrite = shallowRef(false)
  const renewing = shallowRef(false)
  const displayedNames = shallowRef<readonly string[] | undefined>(undefined)

  let active = false
  let generation = 0
  let timer: ReturnType<typeof setTimeout> | undefined
  let polling = false
  let pollAgain = false
  let runningPoll = Promise.resolve()
  let writeEpoch = 0
  let displayEpoch = 0
  let initialClaimAttempted = false
  let lastRenewed = 0
  let resolveOperation: ((value: InstrumentStatus | null) => void) | null = null

  const hasControl = computed(() => Boolean(connected.value && renewing.value
    && status.value?.control.hasControl && !status.value.control.releasing))
  const canWrite = computed(() => Boolean(hasControl.value && !controlBusy.value && !writeInFlight.value
    && !uncertainWrite.value && !pendingOperation.value && !status.value?.pending
    && status.value && !['Faulted', 'Closed', 'Uninitialized'].includes(status.value.state)))
  const canConfigure = computed(() => canWrite.value && status.value?.state === 'Ready'
    && status.value.config.sweepMode === 'HOLD')

  const frameCondition = computed<FrameCondition>(() => {
    const currentStatus = status.value
    const currentFrame = frame.value
    if (!currentFrame) {
      if (currentStatus?.state === 'Faulted') return { kind: 'fault', label: 'FAULT', detail: currentStatus.error }
      return { kind: 'empty', label: 'NO DATA', detail: 'Run a single or continuous sweep to acquire measurement data.' }
    }
    if (!connected.value) return { kind: 'disconnected', label: 'DISCONNECTED', detail: 'Retained frame · connection unavailable' }
    if (currentStatus?.state === 'Faulted') return { kind: 'fault', label: 'FAULT', detail: 'Retained frame · ' + currentStatus.error }
    if (!currentStatus?.activeConfigValid || currentFrame.revision !== currentStatus.revision) {
      return { kind: 'stale', label: 'PREVIOUS CONFIG', detail: 'Retained frame · acquire data for the applied configuration' }
    }
    if (currentFrame.runId !== currentStatus.runId) {
      return { kind: 'acquiring', label: 'PREVIOUS RUN', detail: 'Retained frame · waiting for current run' }
    }
    if (!frameCurrent.value) {
      if (currentStatus.pending || pendingOperation.value) {
        return { kind: 'acquiring', label: 'ACQUIRING', detail: 'Retained frame · operation in progress' }
      }
      return { kind: 'stale', label: 'RETAINED FRAME', detail: frameError.value || 'Current data unavailable' }
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
    if (polling) { pollAgain = true; return }
    timer = setTimeout(() => { timer = undefined; void refresh() }, delay)
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
      const conflict = failure instanceof ApiError && [403, 409].includes(failure.status)
      notify(automatic && conflict ? '另一客户端持有控制权，当前只读查看数据。' : message(failure), conflict ? 'warning' : 'error')
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
      if (epoch === generation) { controlBusy.value = false; schedule(0) }
    }
  }

  async function observeOperation(epoch: number): Promise<void> {
    const pending = pendingOperation.value
    if (!pending) return
    try {
      // 已受理操作的凭据始终属于原令牌，释放或重新获取控制租约后也不转移。
      const result = await api.operation(pending.id, pending.ownerSessionId)
      if (epoch !== generation || pendingOperation.value?.id !== pending.id || result.state === 'pending') return
      if (result.operationId !== pending.id) throw new ApiError('服务返回了不匹配的操作编号', 0, '', 'protocol')
      if (result.state === 'failed') {
        pendingOperation.value = null
        notify(result.result?.message || '设备操作失败，请检查读回状态。', 'error')
        settleOperation(null)
        return
      }
      pendingOperation.value = { ...pending, readingBack: true }
      const readback = await api.status()
      if (epoch !== generation || pendingOperation.value?.id !== pending.id) return
      status.value = readback
      connected.value = true
      pendingOperation.value = null
      error.value = null
      notify(`${pending.label} 完成，已读回设备状态。`, 'success')
      settleOperation(readback)
    } catch (failure) {
      if (epoch !== generation) return
      if (failure instanceof ApiError && failure.status === 404) {
        pendingOperation.value = null
        notify('操作结果已过期；请根据设备读回状态判断结果，未自动重发。', 'warning')
        settleOperation(null)
      }
      // 暂时无法查询状态时保留操作凭据；HTTP 202 只表示已受理，不表示操作完成。
    }
  }

  async function pollCycle(epoch: number): Promise<void> {
    const wasConnected = connected.value
    const epochAtRead = writeEpoch
    const displayAtRead = displayEpoch
    try {
      const [statusResult, frameResult] = await Promise.allSettled([api.status(), api.frame(displayedNames.value)])
      if (epoch !== generation || !active) return
      if (statusResult.status === 'fulfilled') {
        status.value = statusResult.value
        connected.value = true
        if (epochAtRead === writeEpoch && !writeInFlight.value && !statusResult.value.pending) uncertainWrite.value = false
      } else {
        connected.value = false
        if (wasConnected || !initialized.value) notify(message(statusResult.reason), 'error')
      }
      if (displayAtRead !== displayEpoch) {
        pollAgain = true
      } else if (frameResult.status === 'fulfilled') {
        if (frameResult.value.available) {
          frame.value = frameResult.value
          frameCurrent.value = frameResult.value.current
          frameError.value = ''
        } else {
          frameCurrent.value = false
          frameError.value = frameResult.value.reason
        }
      } else {
        frameCurrent.value = false
        frameError.value = message(frameResult.reason)
        if (frameResult.reason instanceof ApiError && frameResult.reason.status === 0) connected.value = false
      }
      initialized.value = true
      if (connected.value) {
        await observeOperation(epoch)
        if (epoch !== generation || !active) return
        const control = status.value?.control
        if (!initialClaimAttempted) {
          initialClaimAttempted = true
          if (control?.hasControl || (control?.owner === 'none' && !control.releasing)) await claim(true)
          else notify('控制权已被其他客户端持有；当前只读查看数据。', 'warning')
        } else if (renewing.value && control?.hasControl && !control.releasing && !controlBusy.value
          && now() - lastRenewed >= (options.renewMs ?? 10000)) {
          await claim(true)
        }
        if (epoch !== generation || !active) return
        if (!wasConnected && notice.value?.text === INITIAL_NOTICE) {
          notify('RAW 接收波与 S 参数 · 未校准')
        }
        if (!wasConnected && notice.value?.tone === 'error' && !pendingOperation.value) {
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
    if (polling) { pollAgain = true; return runningPoll }
    if (timer !== undefined) clearTimeout(timer)
    timer = undefined
    polling = true
    pollAgain = false
    const epoch = generation
    runningPoll = pollCycle(epoch).finally(() => {
      polling = false
      if (!active) return
      if (epoch !== generation) { schedule(0); return }
      schedule(pollAgain ? 0 : connected.value ? (options.pollMs ?? 200) : (options.retryMs ?? 1000))
    })
    return runningPoll
  }

  async function submit(
    label: string,
    kind: PendingOperation['kind'],
    request: () => Promise<AcceptedOperation>,
    configuration = false,
  ): Promise<InstrumentStatus | null> {
    if (!(configuration ? canConfigure.value : canWrite.value)) {
      notify(status.value?.control.readOnlyReason || '请取得控制权，并等待当前操作完成；修改配置前须先 Hold。', 'warning')
      return null
    }
    writeInFlight.value = true
    ++writeEpoch
    const epoch = generation
    const ownerSessionId = api.sessionId
    try {
      const accepted = await request()
      if (epoch !== generation) return null
      const completion = new Promise<InstrumentStatus | null>((resolve) => { resolveOperation = resolve })
      pendingOperation.value = { id: accepted.operationId, label, kind, ownerSessionId, readingBack: false }
      notify(`${label} 已受理，等待设备完成。`)
      return completion
    } catch (failure) {
      if (epoch !== generation) return null
      uncertainWrite.value = failure instanceof ApiError && failure.status === 0 && failure.kind !== 'cancelled'
      notify(uncertainWrite.value
        ? `${message(failure)}；受理结果暂无法确认，等待读回服务状态，未自动重发。`
        : message(failure), 'error')
      return null
    } finally {
      if (epoch === generation) { writeInFlight.value = false; schedule(0) }
    }
  }

  function start(): void {
    if (active) return
    active = true
    ++generation
    schedule(0)
  }

  /** 显示集合与测量目录分离；旧集合的迟到帧不能覆盖新选择。 */
  function setDisplayedMeasurements(names: readonly string[]): void {
    if (names.length > 4 || new Set(names).size !== names.length) throw new Error('最多显示四条不同迹线')
    if (JSON.stringify(names) === JSON.stringify(displayedNames.value)) return
    displayedNames.value = [...names]
    ++displayEpoch
    if (frame.value) frame.value = { ...frame.value, traces: frame.value.traces.filter(trace => names.includes(trace.name)) }
    frameCurrent.value = false
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
    status, frame, connected, initialized, frameCurrent, frameError, pendingOperation, error, notice,
    controlBusy, writeInFlight, uncertainWrite, hasControl, canWrite, canConfigure, frameCondition,
    start, stop, refresh, setDisplayedMeasurements, claim: () => claim(false), release,
    dismissNotice: () => { notice.value = null },
    configure: (candidate: SweepConfiguration, expectedRevision: string) =>
      submit('Apply configuration', 'config', () => api.configure(candidate, expectedRevision), true),
    addMeasurement: (candidate: MeasurementCandidate, expectedRevision: string) =>
      submit('Add measurement', 'measurement', () => api.addMeasurement(candidate, expectedRevision), true),
    updateMeasurement: (candidate: MeasurementCandidate, expectedRevision: string) =>
      submit('Change measurement', 'measurement', () => api.updateMeasurement(candidate, expectedRevision), true),
    deleteMeasurement: (name: string, expectedRevision: string) =>
      submit('Delete measurement', 'measurement', () => api.deleteMeasurement(name, expectedRevision), true),
    run: (action: RunAction) => submit(ACTION_LABEL[action], 'sweep', () => api.operate(action)),
  }
}

export type InstrumentModel = ReturnType<typeof useInstrument>
