import {
  decodeAccepted,
  decodeClaim,
  decodeFrame,
  decodeOperation,
  decodeStatus,
} from './decoders'
import type {
  AcceptedOperation,
  ClaimResponse,
  ChannelCommand,
  FrameResponse,
  InstrumentStatus,
  MeasurementCandidate,
  OperationSnapshot,
  RunAction,
  SweepControlCommand,
  StimulusZoomCommand,
  SweepPatch,
  DisplayCommand,
  TraceDestination,
  TraceCreationOptions,
} from './types'
import type { CalibrationCommand, CalibrationFileFormat, CalibrationPreviewResponse, CalibrationSnapshot } from './calibrationTypes'
import { decodeCalibrationFile } from './calibrationFiles'
import { decodeCalibration, decodeCalibrationPreview } from './calibrationDecoders'

export class ApiError extends Error {
  constructor(
    message: string,
    readonly status = 0,
    readonly code = '',
    readonly kind: 'http' | 'transport' | 'protocol' | 'cancelled' = 'http',
  ) {
    super(message)
    this.name = 'ApiError'
  }
}

interface SessionStorage {
  getItem(key: string): string | null
  setItem(key: string, value: string): void
  removeItem(key: string): void
}

interface ApiOptions {
  root?: string
  fetch?: typeof fetch
  storage?: SessionStorage | null
  timeoutMs?: number
  diagnostics?: boolean
}

const SESSION_KEY = 'vnapro.web.session'

function browserStorage(): SessionStorage | null {
  try {
    return typeof sessionStorage === 'undefined' ? null : sessionStorage
  } catch {
    return null
  }
}

function browserFrameDiagnostics(): boolean {
  try {
    return (
      typeof location !== 'undefined' &&
      new URLSearchParams(location.search).get('debug') === 'frames'
    )
  } catch {
    return false
  }
}

function requestId(): string {
  if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID()
  const bytes = new Uint8Array(16)
  if (globalThis.crypto?.getRandomValues)
    globalThis.crypto.getRandomValues(bytes)
  else
    for (let index = 0; index < bytes.length; index++)
      bytes[index] = Math.floor(Math.random() * 256)
  return Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join(
    '',
  )
}

function measurementBody(
  candidate: MeasurementCandidate,
): Record<string, unknown> {
  return 'parameter' in candidate
    ? { parameter: candidate.parameter }
    : {
        kind: candidate.kind,
        receiverKind: candidate.receiverKind,
        receiverPort: candidate.receiverPort,
        sourcePort: candidate.sourcePort,
      }
}

export interface InstrumentGateway {
  readonly sessionId: string
  status(): Promise<InstrumentStatus>
  frame(names?: readonly string[]): Promise<FrameResponse>
  /** 后台续租失败直接报告失权；只有显式获取允许重新申请令牌。 */
  claim(renewOnly?: boolean): Promise<ClaimResponse>
  release(): Promise<void>
  configure(
    candidate: SweepPatch,
    expectedRevision: string,
    channelId?: number,
  ): Promise<AcceptedOperation>
  operate(action: RunAction): Promise<AcceptedOperation>
  sweepControl?(command: SweepControlCommand, channelId?: number): Promise<AcceptedOperation>
  stimulusZoom?(command: StimulusZoomCommand, expectedRevision: string, expectedDisplayRevision: string, channelId?: number): Promise<AcceptedOperation>
  channel?(command: ChannelCommand, expectedRevision: string, expectedDisplayRevision: string): Promise<AcceptedOperation>
  operation(id: string, ownerSessionId: string): Promise<OperationSnapshot>
  addMeasurement(
    candidate: MeasurementCandidate,
    expectedRevision: string,
  ): Promise<AcceptedOperation>
  updateMeasurement(
    candidate: MeasurementCandidate,
    expectedRevision: string,
  ): Promise<AcceptedOperation>
  deleteMeasurement(
    name: string,
    expectedRevision: string,
  ): Promise<AcceptedOperation>
  display(
    command: DisplayCommand,
    expectedDisplayRevision: string,
  ): Promise<AcceptedOperation>
  cloneMeasurement(
    sourceName: string,
    expectedRevision: string,
    expectedDisplayRevision: string,
    destination?: TraceDestination,
  ): Promise<AcceptedOperation>
  createTrace(
    expectedRevision: string,
    expectedDisplayRevision: string,
    destination?: TraceCreationOptions,
  ): Promise<AcceptedOperation>
  createAllSParameters(
    expectedRevision: string,
    expectedDisplayRevision: string,
  ): Promise<AcceptedOperation>
  deleteDiagram(
    diagram: number,
    expectedRevision: string,
    expectedDisplayRevision: string,
  ): Promise<AcceptedOperation>
  abortRequests(): void
  calibration?(command: CalibrationCommand, expectedRevision: string, channelId?: number): Promise<AcceptedOperation>
  calibrationStatus?(): Promise<CalibrationSnapshot>
  exportCalibration?(setId: string, channelId?: number, format?: CalibrationFileFormat): Promise<Record<string, unknown>>
  calibrationPreview?(sessionId: string, stepId: string, channelId?: number): Promise<CalibrationPreviewResponse>
}

/** 仅负责 JSON 传输；设备操作的完成状态和控制权由服务端管理。 */
export class InstrumentApi implements InstrumentGateway {
  private readonly root: string
  private readonly fetcher: typeof fetch
  private readonly storage: SessionStorage | null
  private readonly timeoutMs: number
  private readonly diagnostics: boolean
  private readonly controllers = new Set<AbortController>()
  private token = ''

  constructor(options: ApiOptions = {}) {
    this.root = options.root ?? '/api/v1'
    this.fetcher = options.fetch ?? globalThis.fetch.bind(globalThis)
    this.storage =
      options.storage === undefined ? browserStorage() : options.storage
    this.timeoutMs = options.timeoutMs ?? 5000
    this.diagnostics = options.diagnostics ?? browserFrameDiagnostics()
    try {
      this.token = this.storage?.getItem(SESSION_KEY) || ''
    } catch {
      /* 浏览器存储可能不可用。 */
    }
  }

  get sessionId(): string {
    return this.token
  }

  abortRequests(): void {
    for (const controller of this.controllers) controller.abort()
    this.controllers.clear()
  }

  private rememberSession(token: string): void {
    this.token = token
    try {
      if (token) this.storage?.setItem(SESSION_KEY, token)
      else this.storage?.removeItem(SESSION_KEY)
    } catch {
      /* 控制租约仍可保存在当前标签页内存中使用。 */
    }
  }

  private async request<T>(
    path: string,
    decode: (value: unknown) => T,
    method = 'GET',
    body?: unknown,
    ownerSessionId = this.token,
    expectedStatus?: number,
  ): Promise<T> {
    const diagnostic = this.diagnostics && path.split('?')[0] === '/frame'
    const started = diagnostic ? performance.now() : 0
    let receiveMs = 0
    let jsonMs = 0
    let payloadBytes = 0
    let stage = 'receive'
    const controller = new AbortController()
    this.controllers.add(controller)
    let timedOut = false
    const timeout = setTimeout(() => {
      timedOut = true
      controller.abort()
    }, this.timeoutMs)
    try {
      const headers: Record<string, string> = { Accept: 'application/json' }
      if (ownerSessionId) headers['X-Vna-Session'] = ownerSessionId
      if (body !== undefined) headers['Content-Type'] = 'application/json'
      const response = await this.fetcher(`${this.root}${path}`, {
        method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        cache: 'no-store',
        signal: controller.signal,
      })
      let value: unknown
      try {
        if (diagnostic) {
          const payload = await response.text()
          receiveMs = performance.now() - started
          payloadBytes = new TextEncoder().encode(payload).byteLength
          stage = 'json'
          const parsingStarted = performance.now()
          value = JSON.parse(payload)
          jsonMs = performance.now() - parsingStarted
        } else {
          value = await response.json()
        }
      } catch (error) {
        // 正文流读取失败交给传输层；只有 JSON 语法错误属于损坏协议。
        if (controller.signal.aborted || !(error instanceof SyntaxError))
          throw error
        throw new ApiError(
          `服务返回无效 JSON (${response.status})`,
          response.ok ? 0 : response.status,
          '',
          'protocol',
        )
      }
      stage = 'http'
      if (!response.ok) {
        const item =
          value !== null && typeof value === 'object'
            ? (value as Record<string, unknown>)
            : {}
        const error =
          item.error !== null && typeof item.error === 'object'
            ? (item.error as Record<string, unknown>)
            : {}
        throw new ApiError(
          typeof error.message === 'string'
            ? error.message
            : `HTTP ${response.status}`,
          response.status,
          typeof error.code === 'string' ? error.code : '',
          'http',
        )
      }
      if (expectedStatus !== undefined && response.status !== expectedStatus) {
        throw new ApiError(
          `服务返回意外状态 (${response.status})`,
          0,
          '',
          'protocol',
        )
      }
      try {
        stage = 'decode'
        const decodingStarted = diagnostic ? performance.now() : 0
        const result = decode(value)
        if (diagnostic) {
          const frame = result as FrameResponse
          // 仅打印版本与传输指标，禁止把 IQ、标记读数或控制令牌写入控制台。
          console.debug('[VnaPro][frame]', {
            available: frame.available,
            ...(frame.available
              ? {
                  frameSeq: frame.frameSeq,
                  runId: frame.runId,
                  displayRevision: frame.displayRevision,
                  traceCount: frame.traces.length,
                  originalPointCount: frame.originalPointCount,
                  displayPointCount: frame.displayPointCount,
                }
              : {}),
            payloadBytes,
            receiveMs,
            jsonMs,
            decodeMs: performance.now() - decodingStarted,
          })
        }
        return result
      } catch (error) {
        throw new ApiError(
          error instanceof Error ? error.message : '服务数据格式无效',
          0,
          '',
          'protocol',
        )
      }
    } catch (error) {
      if (diagnostic) {
        console.debug('[VnaPro][frame][failed]', {
          stage,
          payloadBytes,
          elapsedMs: performance.now() - started,
          kind:
            error instanceof ApiError
              ? error.kind
              : controller.signal.aborted && !timedOut
                ? 'cancelled'
                : 'transport',
        })
      }
      if (error instanceof ApiError) throw error
      if (controller.signal.aborted && !timedOut)
        throw new ApiError('界面请求已停止', 0, '', 'cancelled')
      throw new ApiError(
        timedOut ? '采集服务响应超时' : '无法连接采集服务',
        0,
        '',
        'transport',
      )
    } finally {
      clearTimeout(timeout)
      this.controllers.delete(controller)
    }
  }

  status(): Promise<InstrumentStatus> {
    return this.request('/status', decodeStatus)
  }
  calibrationStatus(channelId?: number): Promise<CalibrationSnapshot> {
    return this.request(`/calibration${channelId === undefined ? '' : `?channelId=${channelId}`}`, decodeCalibration)
  }
  calibration(command: CalibrationCommand, expectedRevision: string, channelId?: number): Promise<AcceptedOperation> {
    return this.request('/calibration', decodeAccepted, 'POST',
      { ...command, ...(channelId === undefined ? {} : { channelId }), expectedRevision, requestId: requestId() }, this.token, 202)
  }
  exportCalibration(setId: string, channelId?: number, format?: CalibrationFileFormat): Promise<Record<string, unknown>> {
    const query = [...(channelId === undefined ? [] : [`channelId=${channelId}`]), ...(format ? [`format=${format}`] : [])].join('&')
    return this.request(`/calibration/sets/${encodeURIComponent(setId)}${query ? `?${query}` : ''}`,
      decodeCalibrationFile)
  }
  calibrationPreview(sessionId: string, stepId: string, channelId?: number): Promise<CalibrationPreviewResponse> {
    return this.request(`/calibration/preview?sessionId=${encodeURIComponent(sessionId)}&stepId=${encodeURIComponent(stepId)}${channelId === undefined ? '' : `&channelId=${channelId}`}`, decodeCalibrationPreview)
  }
  frame(names?: readonly string[]): Promise<FrameResponse> {
    const query =
      names === undefined
        ? ''
        : names.length === 0
          ? '?display=none'
          : `?${names.map((name) => `trace=${encodeURIComponent(name)}`).join('&')}`
    const encoding = query ? '&encoding=packed' : '?encoding=packed'
    return this.request(`/frame${query}${encoding}`, decodeFrame)
  }

  async claim(renewOnly = false): Promise<ClaimResponse> {
    let result: ClaimResponse
    try {
      result = await this.request(
        '/control/claim',
        decodeClaim,
        'POST',
        this.token ? { sessionId: this.token } : {},
      )
    } catch (error) {
      if (renewOnly || !(error instanceof ApiError) || error.status !== 403 || !this.token)
        throw error
      // 控制租约令牌只由服务端签发；过期令牌不能在本地恢复。
      this.rememberSession('')
      result = await this.request('/control/claim', decodeClaim, 'POST', {})
    }
    this.rememberSession(result.sessionId)
    return result
  }

  async release(): Promise<void> {
    await this.request(
      '/control/release',
      (value) => {
        if (
          value === null ||
          typeof value !== 'object' ||
          (value as Record<string, unknown>).releasing !== true
        ) {
          throw new Error('服务未确认控制权释放')
        }
      },
      'POST',
      {},
    )
  }

  configure(
    candidate: SweepPatch,
    expectedRevision: string,
    channelId?: number,
  ): Promise<AcceptedOperation> {
    const fields = [
      'startHz',
      'stopHz',
      'centerHz',
      'spanHz',
      'points',
      'ifBandwidthHz',
      'powerDbm',
        'sweepTimeAuto',
        'sweepTimeSeconds',
    ] as const
    const patch = Object.fromEntries(
      fields
        .filter((key) => candidate[key] !== undefined)
        .map((key) => [key, candidate[key]]),
    )
    return this.request(
      '/config',
      decodeAccepted,
      'POST',
      { ...patch, ...(channelId === undefined ? {} : { channelId }), expectedRevision, requestId: requestId() },
      this.token,
      202,
    )
  }

  operate(action: RunAction): Promise<AcceptedOperation> {
    return this.request(
      '/operations',
      decodeAccepted,
      'POST',
      { action, requestId: requestId() },
      this.token,
      202,
    )
  }

  sweepControl(command: SweepControlCommand, channelId?: number): Promise<AcceptedOperation> {
    return this.request(
      '/sweep-control',
      decodeAccepted,
      'POST',
      { ...command, ...(channelId === undefined ? {} : { channelId }), requestId: requestId() },
      this.token,
      202,
    )
  }

  stimulusZoom(command: StimulusZoomCommand, expectedRevision: string, expectedDisplayRevision: string, channelId?: number): Promise<AcceptedOperation> {
    return this.request('/stimulus-zoom', decodeAccepted, 'POST',
      { ...command, ...(channelId === undefined ? {} : { channelId }), expectedRevision, expectedDisplayRevision, requestId: requestId() }, this.token, 202)
  }

  channel(command: ChannelCommand, expectedRevision: string, expectedDisplayRevision: string): Promise<AcceptedOperation> {
    return this.request('/channels', decodeAccepted, 'POST',
      { ...command, expectedRevision, expectedDisplayRevision, requestId: requestId() }, this.token, 202)
  }

  operation(id: string, ownerSessionId: string): Promise<OperationSnapshot> {
    return this.request(
      `/operations/${encodeURIComponent(id)}`,
      decodeOperation,
      'GET',
      undefined,
      ownerSessionId,
    )
  }

  addMeasurement(
    candidate: MeasurementCandidate,
    expectedRevision: string,
  ): Promise<AcceptedOperation> {
    return this.request(
      '/measurements',
      decodeAccepted,
      'POST',
      {
        ...measurementBody(candidate),
        name: candidate.name,
        expectedRevision,
        requestId: requestId(),
      },
      this.token,
      202,
    )
  }

  updateMeasurement(
    candidate: MeasurementCandidate,
    expectedRevision: string,
  ): Promise<AcceptedOperation> {
    return this.request(
      `/measurements/${encodeURIComponent(candidate.name)}`,
      decodeAccepted,
      'PUT',
      {
        ...measurementBody(candidate),
        expectedRevision,
        requestId: requestId(),
      },
      this.token,
      202,
    )
  }

  deleteMeasurement(
    name: string,
    expectedRevision: string,
  ): Promise<AcceptedOperation> {
    return this.request(
      `/measurements/${encodeURIComponent(name)}`,
      decodeAccepted,
      'DELETE',
      { expectedRevision, requestId: requestId() },
      this.token,
      202,
    )
  }
  display(
    command: DisplayCommand,
    expectedDisplayRevision: string,
  ): Promise<AcceptedOperation> {
    return this.request(
      '/display',
      decodeAccepted,
      'POST',
      { ...command, expectedDisplayRevision, requestId: requestId() },
      this.token,
      202,
    )
  }

  cloneMeasurement(
    sourceName: string,
    expectedRevision: string,
    expectedDisplayRevision: string,
    destination: TraceDestination = {},
  ): Promise<AcceptedOperation> {
    return this.request(
      '/measurement-actions/clone',
      decodeAccepted,
      'POST',
      {
        sourceName,
        ...destination,
        expectedRevision,
        expectedDisplayRevision,
        requestId: requestId(),
      },
      this.token,
      202,
    )
  }

  createTrace(
    expectedRevision: string,
    expectedDisplayRevision: string,
    destination: TraceCreationOptions = {},
  ): Promise<AcceptedOperation> {
    return this.request(
      '/measurement-actions/add',
      decodeAccepted,
      'POST',
      {
        newDiagram: false,
        ...destination,
        expectedRevision,
        expectedDisplayRevision,
        requestId: requestId(),
      },
      this.token,
      202,
    )
  }

  createAllSParameters(
    expectedRevision: string,
    expectedDisplayRevision: string,
  ): Promise<AcceptedOperation> {
    return this.request(
      '/measurement-actions/all-sparameters',
      decodeAccepted,
      'POST',
      {
        expectedRevision,
        expectedDisplayRevision,
        confirmed: true,
        requestId: requestId(),
      },
      this.token,
      202,
    )
  }

  deleteDiagram(
    diagram: number,
    expectedRevision: string,
    expectedDisplayRevision: string,
  ): Promise<AcceptedOperation> {
    return this.request(
      '/measurement-actions/delete-diagram',
      decodeAccepted,
      'POST',
      {
        diagram,
        expectedRevision,
        expectedDisplayRevision,
        requestId: requestId(),
      },
      this.token,
      202,
    )
  }
}
