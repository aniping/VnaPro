import {
  MARKER_COUNT,
  MARKER_IDS,
  type MarkerDefinition,
  type MarkerState,
} from '../domain/markers'
import type { MarkerProjection, TraceProjection } from './types'
import { array, flag, integer, invalid, number, object } from './decodeValues'

export function decodeMarkerState(value: unknown): MarkerState {
  const item = object(value, 'display.marker')
  return {
    enabled: flag(item.enabled, 'display.marker.enabled'),
    frequencyHz:
      item.frequencyHz === null
        ? null
        : number(item.frequencyHz, 'display.marker.frequencyHz', 0),
    sampleIndex:
      item.sampleIndex === null
        ? null
        : integer(item.sampleIndex, 'display.marker.sampleIndex', 0, 10000),
    discrete: flag(item.discrete, 'display.marker.discrete'),
  }
}

function markerId(value: unknown): number {
  return integer(value, 'marker.id', 1, MARKER_COUNT)
}

function completeIds(markers: readonly { id: number }[]): void {
  if (
    markers.length !== MARKER_COUNT ||
    new Set(markers.map((marker) => marker.id)).size !== MARKER_COUNT
  ) {
    invalid('marker.ids')
  }
}

function equalState(left: MarkerState, right: MarkerState): boolean {
  return (
    left.enabled === right.enabled &&
    left.frequencyHz === right.frequencyHz &&
    left.sampleIndex === right.sampleIndex &&
    left.discrete === right.discrete
  )
}

/** 新协议字段必须一起出现；旧服务整体缺省时仅恢复原来的 M1，其他槽位保持关闭。 */
export function decodeMarkerCollection(
  item: Record<string, unknown>,
  alias: MarkerState,
): {
  markers: MarkerDefinition[]
  activeMarkerId: number
} {
  if (item.markers === undefined && item.activeMarkerId === undefined) {
    return {
      activeMarkerId: 1,
      markers: MARKER_IDS.map((id) =>
        id === 1
          ? { id, ...alias }
          : {
              id,
              enabled: false,
              frequencyHz: null,
              sampleIndex: null,
              discrete: false,
            },
      ),
    }
  }
  const activeMarkerId = markerId(item.activeMarkerId)
  const markers = array(item.markers, 'display.markers', MARKER_COUNT).map(
    (value) => {
      const marker = object(value, 'display.marker')
      return { id: markerId(marker.id), ...decodeMarkerState(marker) }
    },
  )
  completeIds(markers)
  if (
    !equalState(alias, markers.find((marker) => marker.id === activeMarkerId)!)
  ) {
    invalid('display.markerAlias')
  }
  return { markers, activeMarkerId }
}

export function decodeMarkerProjections(
  item: Record<string, unknown>,
  alias: MarkerProjection,
  decode: (value: unknown) => MarkerProjection,
): Pick<TraceProjection, 'markers' | 'activeMarkerId'> {
  if (item.markers === undefined && item.activeMarkerId === undefined) {
    return {
      activeMarkerId: 1,
      markers: MARKER_IDS.map((id) => ({
        ...alias,
        ...(id === 1
          ? {}
          : {
              available: false,
              valid: false,
              reason: 'Marker off',
              sampleIndex: null,
              frequencyHz: null,
              real: null,
              imag: null,
              value: null,
              secondaryValue: null,
              valueState: 'undefined' as const,
              secondaryState: 'undefined' as const,
              impedanceReal: null,
              impedanceImag: null,
              impedanceState: 'invalid' as const,
              interpolated: false,
            }),
        id,
      })),
    }
  }
  const activeMarkerId = markerId(item.activeMarkerId)
  const markers = array(item.markers, 'projection.markers', MARKER_COUNT).map(
    (value) => {
      const marker = object(value, 'projection.marker')
      return { id: markerId(marker.id), ...decode(marker) }
    },
  )
  completeIds(markers)
  const selected = markers.find((marker) => marker.id === activeMarkerId)!
  const { id: _id, ...projection } = selected
  if (JSON.stringify(alias) !== JSON.stringify(projection)) {
    invalid('projection.markerAlias')
  }
  return { markers, activeMarkerId }
}
