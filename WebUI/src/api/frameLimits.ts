import { MAX_TRACES } from '../domain/presentation'

/** 所有显示迹线都传完整扫频；传输容量按迹线资源与单条采样上限约束。 */
export const FRAME_POINT_LIMIT = 10001
export const FRAME_SAMPLE_BUDGET = FRAME_POINT_LIMIT * MAX_TRACES
