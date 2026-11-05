export type ForecastStatus =
  | 'OK'
  | 'NO_DATA'
  | 'EXPIRED'
  | 'MODEL_UNAVAILABLE'
  | 'DEPENDENCY_UNAVAILABLE'
  | 'PARAM_INVALID'

export interface ForecastPoint {
  targetStart: string
  targetEnd: string
  horizonHours: number
  energyKwh: string | null
  meanPowerKw: string | null
  freePilesExpected: number | null
  nullReason?: string | null
}

export interface ForecastEnvelope {
  contractVersion: 'forecast_v1'
  originTime: string
  targetStart: string
  targetEnd: string
  horizonHours: number
  analysisStationId: string
  businessStationId: number | null
  dataMode: string
  datasetVersion: string | null
  modelMode: string
  status: ForecastStatus
  degraded: boolean
  reasons: string[]
  points: ForecastPoint[]
  sourceMode: string
  generatedAt: string
}

export interface RecommendationItem {
  stationId: number
  name: string
  rank: number
  score: string
  distanceKm: string | null
  priceYuanPerKwh: string | null
  currentAvailabilityRatio: string | null
  predictedAvailabilityRatio: string | null
  reasons: string[]
  modelMode: string
}

export interface RecommendationEnvelope {
  contractVersion: 'member3_recommendation_v1'
  status: 'OK' | 'NO_DATA' | 'DEPENDENCY_UNAVAILABLE' | 'PARAM_INVALID'
  degraded: boolean
  reasons: string[]
  datasetVersion: string | null
  runId: string | null
  modelMode: string
  source: string
  origin: {
    latitude: number
    longitude: number
    arrivalTime: string | null
    arrivalTimeUsed: boolean
    arrivalTimeReason: string | null
  }
  scoring: { criteriaUsed?: string[]; criteriaExcluded?: string[]; declaredWeights?: Record<string, string> }
  recommendations: RecommendationItem[]
}

export interface HealthEnvelope {
  service: string
  framework: 'Flask'
  status: ForecastStatus
  degraded: boolean
  reasons: string[]
  artifacts: Record<string, unknown>
  businessSnapshot: { available: boolean; readOnly: boolean }
}

export interface HiveReportEnvelope {
  status: 'OK' | 'NO_DATA' | 'DEPENDENCY_UNAVAILABLE'
  kind: string
  source: 'HIVE'
  datasetVersion: string | null
  runId: string | null
  generatedAt: string | null
  rowCount?: number
  coverage?: Record<string, unknown>
  reasons: string[]
  rows: Array<Record<string, unknown>>
}

export interface PeakEnvelope {
  status: ForecastStatus
  datasetVersion: string | null
  runId: string | null
  analysisStationId: string
  originTime: string
  horizonHours: number
  topN: number
  peaks: Array<Record<string, unknown>>
  threshold: Record<string, unknown>
  highLoadWindows: Array<Record<string, unknown>>
  warning: {
    level: string
    basis: string
    advice: string[]
    reasons: string[]
    automaticControl: boolean
  }
  reasons: string[]
}

function object(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('响应不是 JSON 对象')
  return value as Record<string, unknown>
}

function string(value: unknown, field: string, nullable = false): string | null {
  if (nullable && (value === null || value === undefined)) return null
  if (typeof value !== 'string') throw new Error(`字段 ${field} 无效`)
  return value
}

function status(value: unknown): ForecastStatus {
  const allowed: ForecastStatus[] = ['OK', 'NO_DATA', 'EXPIRED', 'MODEL_UNAVAILABLE', 'DEPENDENCY_UNAVAILABLE', 'PARAM_INVALID']
  if (typeof value !== 'string' || !allowed.includes(value as ForecastStatus)) throw new Error('状态字段无效')
  return value as ForecastStatus
}

export function parseForecast(value: unknown): ForecastEnvelope {
  const raw = object(value)
  if (raw.contractVersion !== 'forecast_v1') throw new Error('contractVersion 不是 forecast_v1')
  const points = Array.isArray(raw.points) ? raw.points.map(item => {
    const point = object(item)
    if (typeof point.horizonHours !== 'number') throw new Error('预测点 horizonHours 无效')
    return {
      targetStart: string(point.targetStart, 'targetStart')!, targetEnd: string(point.targetEnd, 'targetEnd')!,
      horizonHours: point.horizonHours, energyKwh: string(point.energyKwh, 'energyKwh', true),
      meanPowerKw: string(point.meanPowerKw, 'meanPowerKw', true),
      freePilesExpected: point.freePilesExpected === null ? null : Number(point.freePilesExpected),
      nullReason: string(point.nullReason, 'nullReason', true),
    }
  }) : []
  const rawReasons = Array.isArray(raw.reasons) ? raw.reasons : []
  return {
    contractVersion: 'forecast_v1', originTime: string(raw.originTime, 'originTime')!,
    targetStart: string(raw.targetStart, 'targetStart')!, targetEnd: string(raw.targetEnd, 'targetEnd')!,
    horizonHours: Number(raw.horizonHours), analysisStationId: string(raw.analysisStationId, 'analysisStationId')!,
    businessStationId: raw.businessStationId === null ? null : Number(raw.businessStationId),
    dataMode: string(raw.dataMode, 'dataMode')!, datasetVersion: string(raw.datasetVersion, 'datasetVersion', true),
    modelMode: string(raw.modelMode, 'modelMode')!, status: status(raw.status), degraded: Boolean(raw.degraded),
    reasons: rawReasons.filter(item => typeof item === 'string') as string[], points,
    sourceMode: string(raw.sourceMode, 'sourceMode')!, generatedAt: string(raw.generatedAt, 'generatedAt')!,
  }
}

export function parseRecommendation(value: unknown): RecommendationEnvelope {
  const raw = object(value)
  if (raw.contractVersion !== 'member3_recommendation_v1') throw new Error('recommendation contractVersion 无效')
  const origin = object(raw.origin)
  const recommendations = Array.isArray(raw.recommendations) ? raw.recommendations.map(item => {
    const row = object(item)
    return {
      stationId: Number(row.stationId), name: string(row.name, 'name')!, rank: Number(row.rank), score: string(row.score, 'score')!,
      distanceKm: string(row.distanceKm, 'distanceKm', true), priceYuanPerKwh: string(row.priceYuanPerKwh, 'priceYuanPerKwh', true),
      currentAvailabilityRatio: string(row.currentAvailabilityRatio, 'currentAvailabilityRatio', true),
      predictedAvailabilityRatio: string(row.predictedAvailabilityRatio, 'predictedAvailabilityRatio', true),
      reasons: Array.isArray(row.reasons) ? row.reasons.filter(item => typeof item === 'string') as string[] : [],
      modelMode: string(row.modelMode, 'modelMode')!,
    }
  }) : []
  return {
    contractVersion: 'member3_recommendation_v1', status: raw.status as RecommendationEnvelope['status'], degraded: Boolean(raw.degraded),
    reasons: Array.isArray(raw.reasons) ? raw.reasons.filter(item => typeof item === 'string') as string[] : [],
    datasetVersion: string(raw.datasetVersion, 'datasetVersion', true), runId: string(raw.runId, 'runId', true),
    modelMode: string(raw.modelMode, 'modelMode')!, source: string(raw.source, 'source')!,
    origin: { latitude: Number(origin.latitude), longitude: Number(origin.longitude), arrivalTime: string(origin.arrivalTime, 'arrivalTime', true),
      arrivalTimeUsed: Boolean(origin.arrivalTimeUsed), arrivalTimeReason: string(origin.arrivalTimeReason, 'arrivalTimeReason', true) },
    scoring: object(raw.scoring), recommendations,
  }
}

export function parseHealth(value: unknown): HealthEnvelope {
  const raw = object(value)
  if (raw.framework !== 'Flask') throw new Error('分析服务不是 Flask')
  return { service: string(raw.service, 'service')!, framework: 'Flask', status: status(raw.status), degraded: Boolean(raw.degraded),
    reasons: Array.isArray(raw.reasons) ? raw.reasons.filter(item => typeof item === 'string') as string[] : [], artifacts: object(raw.artifacts),
    businessSnapshot: { available: Boolean(object(raw.businessSnapshot).available), readOnly: Boolean(object(raw.businessSnapshot).readOnly) } }
}

export function parseHive(value: unknown): HiveReportEnvelope {
  const raw = object(value)
  return { status: raw.status as HiveReportEnvelope['status'], kind: string(raw.kind, 'kind')!, source: 'HIVE',
    datasetVersion: string(raw.datasetVersion, 'datasetVersion', true), runId: string(raw.runId, 'runId', true),
    generatedAt: string(raw.generatedAt, 'generatedAt', true), rowCount: raw.rowCount === undefined ? undefined : Number(raw.rowCount),
    coverage: raw.coverage && typeof raw.coverage === 'object' ? raw.coverage as Record<string, unknown> : undefined,
    reasons: Array.isArray(raw.reasons) ? raw.reasons.filter(item => typeof item === 'string') as string[] : [],
    rows: Array.isArray(raw.rows) ? raw.rows.map(item => object(item)) : [] }
}

export function parsePeak(value: unknown): PeakEnvelope {
  const raw = object(value)
  const warning = object(raw.warning)
  return { status: status(raw.status), datasetVersion: string(raw.datasetVersion, 'datasetVersion', true), runId: string(raw.runId, 'runId', true),
    analysisStationId: string(raw.analysisStationId, 'analysisStationId')!, originTime: string(raw.originTime, 'originTime')!, horizonHours: Number(raw.horizonHours),
    topN: Number(raw.topN), peaks: Array.isArray(raw.peaks) ? raw.peaks.map(item => object(item)) : [],
    threshold: object(raw.threshold), highLoadWindows: Array.isArray(raw.highLoadWindows) ? raw.highLoadWindows.map(item => object(item)) : [],
    warning: { level: string(warning.level, 'warning.level')!, basis: string(warning.basis, 'warning.basis')!, advice: Array.isArray(warning.advice) ? warning.advice.filter(item => typeof item === 'string') as string[] : [],
      reasons: Array.isArray(warning.reasons) ? warning.reasons.filter(item => typeof item === 'string') as string[] : [], automaticControl: Boolean(warning.automaticControl) },
    reasons: Array.isArray(raw.reasons) ? raw.reasons.filter(item => typeof item === 'string') as string[] : [] }
}
