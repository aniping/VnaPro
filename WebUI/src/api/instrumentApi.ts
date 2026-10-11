import { decodeAccepted, decodeClaim, decodeFrame, decodeOperation, decodeStatus } from './decoders'
import type {
  AcceptedOperation, ClaimResponse, FrameResponse, InstrumentStatus, MeasurementCandidate,
  OperationSnapshot, RunAction, SweepConfiguration,
} from './types'

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
}

const SESSION_KEY = 'vnapro.web.session'

function browserStorage(): SessionStorage | null {
  try {
    return typeof sessionStorage === 'undefined' ? null : sessionStorage
  } catch {
    return null
  }
}

function requestId(): string {
  if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID()
  const bytes = new Uint8Array(16)
  if (globalThis.crypto?.getRandomValues) globalThis.crypto.getRandomValues(bytes)
  else for (let index = 0; index < bytes.length; index++) bytes[index] = Math.floor(Math.random() * 256)
  return Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join('')
}

export interface InstrumentGateway {
  readonly sessionId: string
  status(): Promise<InstrumentStatus>
  frame(names?: readonly string[]): Promise<FrameResponse>
  claim(): Promise<ClaimResponse>
  release(): Promise<void>
  configure(candidate: SweepConfiguration, expectedRevision: string): Promise<AcceptedOperation>
  operate(action: RunAction): Promise<AcceptedOperation>
  operation(id: string, ownerSessionId: string): Promise<OperationSnapshot>
  addMeasurement(candidate: MeasurementCandidate, expectedRevision: string): Promise<AcceptedOperation>
  deleteMeasurement(name: string, expectedRevision: string): Promise<AcceptedOperation>
  abortRequests(): void
}

/** 仅负责 JSON 传输；设备操作的完成状态和控制权由服务端管理。 */
export class InstrumentApi implements InstrumentGateway {
  private readonly root: string
  private readonly fetcher: typeof fetch
  private readonly storage: SessionStorage | null
  private readonly timeoutMs: number
  private readonly controllers = new Set<AbortController>()
  private token = ''

  constructor(options: ApiOptions = {}) {
    this.root = options.root ?? '/api/v1'
    this.fetcher = options.fetch ?? globalThis.fetch.bind(globalThis)
    this.storage = options.storage === undefined ? browserStorage() : options.storage
    this.timeoutMs = options.timeoutMs ?? 5000
    try { this.token = this.storage?.getItem(SESSION_KEY) || '' } catch { /* 浏览器存储可能不可用。 */ }
  }

  get sessionId(): string { return this.token }

  abortRequests(): void {
    for (const controller of this.controllers) controller.abort()
    this.controllers.clear()
  }

  private rememberSession(token: string): void {
    this.token = token
    try {
      if (token) this.storage?.setItem(SESSION_KEY, token)
      else this.storage?.removeItem(SESSION_KEY)
    } catch { /* 控制租约仍可保存在当前标签页内存中使用。 */ }
  }

  private async request<T>(
    path: string,
    decode: (value: unknown) => T,
    method = 'GET',
    body?: unknown,
    ownerSessionId = this.token,
    expectedStatus?: number,
  ): Promise<T> {
    const controller = new AbortController()
    this.controllers.add(controller)
    let timedOut = false
    const timeout = setTimeout(() => { timedOut = true; controller.abort() }, this.timeoutMs)
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
      try { value = await response.json() } catch {
        throw new ApiError(`服务返回无效 JSON (${response.status})`, response.ok ? 0 : response.status, '', 'protocol')
      }
      if (!response.ok) {
        const item = value !== null && typeof value === 'object' ? value as Record<string, unknown> : {}
        const error = item.error !== null && typeof item.error === 'object' ? item.error as Record<string, unknown> : {}
        throw new ApiError(typeof error.message === 'string' ? error.message : `HTTP ${response.status}`,
          response.status, typeof error.code === 'string' ? error.code : '', 'http')
      }
      if (expectedStatus !== undefined && response.status !== expectedStatus) {
        throw new ApiError(`服务返回意外状态 (${response.status})`, 0, '', 'protocol')
      }
      try { return decode(value) } catch (error) {
        throw new ApiError(error instanceof Error ? error.message : '服务数据格式无效', 0, '', 'protocol')
      }
    } catch (error) {
      if (error instanceof ApiError) throw error
      if (controller.signal.aborted && !timedOut) throw new ApiError('界面请求已停止', 0, '', 'cancelled')
      throw new ApiError(timedOut ? '采集服务响应超时' : '无法连接采集服务', 0, '', 'transport')
    } finally {
      clearTimeout(timeout)
      this.controllers.delete(controller)
    }
  }

  status(): Promise<InstrumentStatus> { return this.request('/status', decodeStatus) }
  frame(names?: readonly string[]): Promise<FrameResponse> {
    const query = names === undefined ? '' : names.length === 0 ? '?display=none'
      : `?${names.map(name => `trace=${encodeURIComponent(name)}`).join('&')}`
    return this.request(`/frame${query}`, decodeFrame)
  }

  async claim(): Promise<ClaimResponse> {
    let result: ClaimResponse
    try {
      result = await this.request('/control/claim', decodeClaim, 'POST', this.token ? { sessionId: this.token } : {})
    } catch (error) {
      if (!(error instanceof ApiError) || error.status !== 403 || !this.token) throw error
      // 控制租约令牌只由服务端签发；过期令牌不能在本地恢复。
      this.rememberSession('')
      result = await this.request('/control/claim', decodeClaim, 'POST', {})
    }
    this.rememberSession(result.sessionId)
    return result
  }

  async release(): Promise<void> {
    await this.request('/control/release', (value) => {
      if (value === null || typeof value !== 'object' || (value as Record<string, unknown>).releasing !== true) {
        throw new Error('服务未确认控制权释放')
      }
    }, 'POST', {})
  }

  configure(candidate: SweepConfiguration, expectedRevision: string): Promise<AcceptedOperation> {
    return this.request('/config', decodeAccepted, 'POST',
      { startHz: candidate.startHz, stopHz: candidate.stopHz, points: candidate.points,
        ifBandwidthHz: candidate.ifBandwidthHz, expectedRevision, requestId: requestId() }, this.token, 202)
  }

  operate(action: RunAction): Promise<AcceptedOperation> {
    return this.request('/operations', decodeAccepted, 'POST', { action, requestId: requestId() }, this.token, 202)
  }

  operation(id: string, ownerSessionId: string): Promise<OperationSnapshot> {
    return this.request(`/operations/${encodeURIComponent(id)}`, decodeOperation, 'GET', undefined, ownerSessionId)
  }

  addMeasurement(candidate: MeasurementCandidate, expectedRevision: string): Promise<AcceptedOperation> {
    return this.request('/measurements', decodeAccepted, 'POST',
      { name: candidate.name, kind: candidate.kind, receiverKind: candidate.receiverKind, receiverPort: candidate.receiverPort,
        sourcePort: candidate.sourcePort, expectedRevision, requestId: requestId() }, this.token, 202)
  }

  deleteMeasurement(name: string, expectedRevision: string): Promise<AcceptedOperation> {
    return this.request(`/measurements/${encodeURIComponent(name)}`, decodeAccepted, 'DELETE',
      { expectedRevision, requestId: requestId() }, this.token, 202)
  }
}
