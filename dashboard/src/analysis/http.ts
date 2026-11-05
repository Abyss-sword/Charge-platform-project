export type Parser<T> = (value: unknown) => T

export class AnalysisHttpError extends Error {
  constructor(public readonly status: number, message: string) { super(message) }
}

export class AnalysisTransport {
  readonly baseUrl: string
  constructor(baseUrl = (import.meta.env.VITE_ANALYSIS_BASE_URL as string | undefined) || 'http://127.0.0.1:8001') {
    this.baseUrl = baseUrl.replace(/\/$/, '')
  }

  async request<T>(path: string, query: Record<string, string>, parser: Parser<T>, signal?: AbortSignal): Promise<T> {
    const url = new URL(`${this.baseUrl}${path}`, window.location.href)
    Object.entries(query).forEach(([key, value]) => url.searchParams.set(key, value))
    const response = await fetch(url, { headers: { Accept: 'application/json' }, signal: signal ?? AbortSignal.timeout(800) })
    let payload: unknown
    try { payload = await response.json() } catch { throw new AnalysisHttpError(response.status, `分析服务返回非 JSON 响应（HTTP ${response.status}）`) }
    if (!response.ok) throw new AnalysisHttpError(response.status, `分析服务 HTTP ${response.status}`)
    try { return parser(payload) } catch (error) { throw new AnalysisHttpError(response.status, error instanceof Error ? error.message : '分析响应契约无效') }
  }
}
