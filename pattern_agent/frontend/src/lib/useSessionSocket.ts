import { useEffect, useReducer, useRef } from 'react'
import type { ChatMessage, ServerEvent, SessionStatus, StepRecord } from './types'

interface SocketState {
  status: SessionStatus
  steps: StepRecord[]
  errors: { message: string; fatal: boolean; step?: number | null }[]
  stopReason: string | null
  finalSummary: string | null
  valUrl: string | null
  connected: boolean
  chatMessages: ChatMessage[]
}

const initialState: SocketState = {
  status: 'idle',
  steps: [],
  errors: [],
  stopReason: null,
  finalSummary: null,
  valUrl: null,
  connected: false,
  chatMessages: [],
}

function stepIndex(steps: StepRecord[], step: number): number {
  return steps.findIndex((s) => s.step === step)
}

function withStep(steps: StepRecord[], step: number, patch: Partial<StepRecord>): StepRecord[] {
  const idx = stepIndex(steps, step)
  if (idx === -1) {
    const next = [...steps, { step, thinking: '', text: '', ...patch }]
    next.sort((a, b) => a.step - b.step)
    return next
  }
  const next = [...steps]
  next[idx] = { ...next[idx], ...patch }
  return next
}

type Action = { type: 'event'; event: ServerEvent } | { type: 'connected' } | { type: 'disconnected' }

function reducer(state: SocketState, action: Action): SocketState {
  if (action.type === 'connected') return { ...state, connected: true }
  if (action.type === 'disconnected') return { ...state, connected: false }

  const event = action.event
  switch (event.type) {
    case 'status_changed':
      return { ...state, status: event.status }
    case 'thinking_delta':
      return {
        ...state,
        steps: withStep(state.steps, event.step, {
          thinking: (stepIndex(state.steps, event.step) >= 0 ? state.steps[stepIndex(state.steps, event.step)].thinking : '') + event.text,
        }),
      }
    case 'text_delta':
      return {
        ...state,
        steps: withStep(state.steps, event.step, {
          text: (stepIndex(state.steps, event.step) >= 0 ? state.steps[stepIndex(state.steps, event.step)].text : '') + event.text,
        }),
      }
    case 'action_started':
      return {
        ...state,
        steps: withStep(state.steps, event.step, { action: { op: event.op, input: event.input } }),
      }
    case 'action_result':
      return {
        ...state,
        steps: withStep(state.steps, event.step, {
          result: { success: event.success, result: event.result, error: event.error },
        }),
      }
    case 'snapshot_ready':
      return {
        ...state,
        steps: withStep(state.steps, event.step, { snapshotUrl: event.url }),
      }
    case 'step_complete':
      return state // steps already updated incrementally; this event just marks the boundary
    case 'session_complete':
      return {
        ...state,
        status: 'complete',
        stopReason: event.reason,
        finalSummary: event.summary,
        valUrl: event.valUrl,
      }
    case 'error':
      return {
        ...state,
        errors: [...state.errors, { message: event.message, fatal: event.fatal, step: event.step }],
        status: event.fatal ? 'error' : state.status,
      }
    case 'user_message':
      return {
        ...state,
        // A new instruction reopens a finished session -- clear the old completion
        // banner (the .val download link stays, it's still the latest saved state
        // until a fresh session_complete overwrites it).
        stopReason: null,
        finalSummary: null,
        chatMessages: [
          ...state.chatMessages,
          { id: `${event.afterStep}-${state.chatMessages.length}`, text: event.text, afterStep: event.afterStep },
        ],
      }
    default:
      return state
  }
}

export function useSessionSocket(sessionId: string | null) {
  const [state, dispatch] = useReducer(reducer, initialState)
  const wsRef = useRef<WebSocket | null>(null)

  useEffect(() => {
    if (!sessionId) return

    const proto = window.location.protocol === 'https:' ? 'wss' : 'ws'
    const ws = new WebSocket(`${proto}://${window.location.host}/ws/${sessionId}`)
    wsRef.current = ws

    ws.onopen = () => dispatch({ type: 'connected' })
    ws.onclose = () => dispatch({ type: 'disconnected' })
    ws.onerror = () => dispatch({ type: 'disconnected' })
    ws.onmessage = (msg) => {
      try {
        const event = JSON.parse(msg.data) as ServerEvent
        dispatch({ type: 'event', event })
      } catch (err) {
        console.error('Failed to parse server event', err, msg.data)
      }
    }

    return () => {
      ws.close()
      wsRef.current = null
    }
  }, [sessionId])

  return state
}
