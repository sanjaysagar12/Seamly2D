export type SessionStatus = 'idle' | 'running' | 'paused' | 'complete' | 'error'

export type ServerEvent =
  | { type: 'thinking_delta'; sessionId: string; step: number; text: string }
  | { type: 'text_delta'; sessionId: string; step: number; text: string }
  | { type: 'action_started'; sessionId: string; step: number; op: string; input: Record<string, unknown> }
  | {
      type: 'action_result'
      sessionId: string
      step: number
      op: string
      success: boolean
      result?: unknown
      error?: unknown
    }
  | { type: 'snapshot_ready'; sessionId: string; step: number; url: string }
  | { type: 'step_complete'; sessionId: string; step: number }
  | {
      type: 'session_complete'
      sessionId: string
      reason: string
      summary: string | null
      valUrl: string | null
    }
  | { type: 'error'; sessionId: string; message: string; fatal: boolean; step?: number | null }
  | { type: 'status_changed'; sessionId: string; status: SessionStatus }
  | { type: 'user_message'; sessionId: string; text: string; afterStep: number }

export interface StepRecord {
  step: number
  thinking: string
  text: string
  action?: { op: string; input: Record<string, unknown> }
  result?: { success: boolean; result?: unknown; error?: unknown }
  snapshotUrl?: string
}

export interface ChatMessage {
  id: string
  text: string
  afterStep: number
}

export interface SessionSummary {
  sessionId: string
  status: SessionStatus
  step: number
  goal: string
  stopReason: string | null
  model: string
}

export interface SessionDetail extends SessionSummary {
  stepLimit: number
  finalSummary: string | null
  valUrl: string | null
  systemPrompt: string
}

export interface ModelOption {
  id: string
  label: string
}

export const STOP_REASON_LABELS: Record<string, string> = {
  agent_complete: 'Agent declared the piece complete',
  step_limit: 'Step limit reached',
  user_stopped: 'Stopped by user',
  api_error: 'Anthropic API error',
  actiond_crash: 'actiond crashed and could not be recovered',
  internal_error: 'Internal orchestrator error',
}
