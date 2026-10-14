import { computed } from 'vue'
import type {
  AcquiredFrame,
  DisplayCommand,
  InstrumentStatus,
} from '../api/types'
import type {
  DisplayFormat,
  ScaleValues,
  LayoutMode,
  InfoTablePosition,
  DisplayAnnotations,
  MarkerInfoLocation,
  MarkerInfoArea,
} from '../domain/presentation'

export interface MarkerCommand {
  markerId?: number
  markerAction?: 'update' | 'add' | 'select' | 'allOff'
  enabled?: boolean
  frequencyHz?: number
  sampleIndex?: number
  step?: number
  discrete?: boolean
}

/** 只读适配后端工作区；仪器动作经统一命令入口执行并等待读回。 */
export function useWorkspace(
  status: () => InstrumentStatus | null,
  frame: () => AcquiredFrame | null,
  send: (command: DisplayCommand) => unknown,
) {
  const presentations = computed(() => status()?.display.presentations ?? {})
  const activeTraceName = computed(
    () => status()?.display.selectedMeasurement ?? '',
  )
  const layout = computed(() => status()?.display.layout ?? 'single')
  const displayedNames = computed(() => status()?.display.displayedNames ?? [])
  const activeMeasurement = computed(() =>
    status()?.measurements.find(
      (trace) => trace.name === activeTraceName.value,
    ),
  )
  const activePresentation = computed(
    () => presentations.value[activeTraceName.value],
  )
  const activeIndex = computed(() => {
    const diagram = status()?.display.diagrams.find((diagram) =>
      diagram.measurementNames.includes(activeTraceName.value),
    )
    return Math.max(
      0,
      diagram?.measurementNames.indexOf(activeTraceName.value) ?? 0,
    )
  })
  const displayedMeasurements = computed(() =>
    displayedNames.value.flatMap((name) => {
      const measurement = status()?.measurements.find(
        (trace) => trace.name === name,
      )
      return measurement ? [measurement] : []
    }),
  )
  const activeFrameTrace = computed(() =>
    frame()?.traces.find((trace) => trace.name === activeTraceName.value),
  )
  const canAutoscale = computed(() =>
    Boolean(
      activeFrameTrace.value?.available &&
        frame()?.current &&
        frame()?.displayRevision === status()?.display.revision &&
        frame()?.revision === status()?.acquisitionRevision &&
        frame()?.measurementRevision === status()?.revision &&
        (!status()?.calibration || frame()?.calibrationRevision === status()?.calibration?.revision) &&
        frame()?.runId === status()?.runId,
    ),
  )

  function selectTrace(traceName: string, diagram?: number) {
    return send({
      action: 'select',
      traceName,
      ...(diagram === undefined ? {} : { diagram }),
    })
  }
  function selectDiagram(diagram: number) {
    return send({ action: 'diagramSelect', diagram })
  }
  function setInfoTable(value: {
    enabled?: boolean
    position?: InfoTablePosition
  }) {
    return send({ action: 'infoTable', ...value })
  }
  function setAnnotations(value: Partial<DisplayAnnotations>) {
    return send({ action: 'displayAnnotations', ...value })
  }
  function setMarkerInfo(enabled: boolean, diagram?: number) {
    return send({
      action: 'markerInfo',
      enabled,
      ...(diagram === undefined ? {} : { diagram }),
    })
  }
  function setMarkerInfoLocation(
    traceName: string,
    location: MarkerInfoLocation,
    area?: MarkerInfoArea,
  ) {
    return send({ action: 'markerInfoLocation', traceName, location, ...(area ? { area } : {}) })
  }
  function overlayAll(diagram?: number) {
    return send({
      action: 'overlay',
      ...(diagram === undefined ? {} : { diagram }),
    })
  }
  function splitAll() {
    return send({ action: 'split' })
  }
  function setDisplayed(traceName: string, enabled: boolean) {
    return send({ action: 'displayed', traceName, enabled })
  }
  function setLayout(value: LayoutMode, diagram?: number) {
    return send({
      action: 'layout',
      layout: value,
      ...(diagram === undefined ? {} : { diagram }),
    })
  }
  function setFormat(format: DisplayFormat) {
    return send({ action: 'format', traceName: activeTraceName.value, format })
  }
  function setScale(scale: ScaleValues) {
    return send({
      action: 'scale',
      traceName: activeTraceName.value,
      perDivision: scale.perDivision,
      referenceLevel: scale.referenceLevel,
      referencePosition: scale.referencePosition ?? 9,
    })
  }
  function autoscale() {
    const acquisitionRevision = status()?.acquisitionRevision
    if (acquisitionRevision)
      return send({
        action: 'autoscale',
        traceName: activeTraceName.value,
        acquisitionRevision,
      })
  }
  function setSmithRadius(radius: number) {
    return send({
      action: 'smithRadius',
      traceName: activeTraceName.value,
      radius,
    })
  }
  function setMarker(value: MarkerCommand, traceName = activeTraceName.value) {
    return send({
      action: 'marker',
      traceName,
      ...value,
      acquisitionRevision: status()?.acquisitionRevision,
    })
  }
  function toggleVisibility(traceName = activeTraceName.value) {
    const current = presentations.value[traceName]
    if (current)
      return send({ action: 'visible', traceName, enabled: !current.visible })
  }
  function addMarker(traceName = activeTraceName.value) {
    return setMarker({ markerAction: 'add' }, traceName)
  }
  function selectMarker(traceName: string, markerId: number) {
    return setMarker({ markerAction: 'select', markerId }, traceName)
  }
  return {
    presentations,
    activeTraceName,
    layout,
    activeMeasurement,
    activePresentation,
    activeIndex,
    displayedNames,
    displayedMeasurements,
    canAutoscale,
    selectTrace,
    selectDiagram,
    setInfoTable,
    setAnnotations,
    setMarkerInfo,
    setMarkerInfoLocation,
    overlayAll,
    splitAll,
    setDisplayed,
    setLayout,
    setFormat,
    setScale,
    setSmithRadius,
    autoscale,
    setMarker,
    addMarker,
    selectMarker,
    toggleVisibility,
  }
}
