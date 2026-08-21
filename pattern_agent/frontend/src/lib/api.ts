import type { ModelOption, SessionDetail, SessionSummary } from './types'

async function req<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, init)
  if (!res.ok) {
    const body = await res.text().catch(() => '')
    throw new Error(`${res.status} ${res.statusText}: ${body}`)
  }
  return res.json() as Promise<T>
}

export async function listMeasurements(): Promise<string[]> {
  const data = await req<{ files: string[] }>('/api/measurements')
  return data.files
}

export async function uploadMeasurement(file: File): Promise<string> {
  const form = new FormData()
  form.append('file', file)
  const data = await req<{ filename: string }>('/api/measurements', {
    method: 'POST',
    body: form,
  })
  return data.filename
}

export async function startSession(params: {
  goal: string
  measurementsFilename?: string
  stepLimit?: number
  autorun?: boolean
  model?: string
  systemPrompt?: string
}): Promise<{ sessionId: string }> {
  return req('/api/sessions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
}

export async function listModels(): Promise<{
  models: ModelOption[]
  default: string
  defaultSystemPrompt: string
}> {
  return req('/api/models')
}

export async function getSession(sessionId: string): Promise<SessionDetail> {
  return req(`/api/sessions/${sessionId}`)
}

export async function listSessions(): Promise<SessionSummary[]> {
  const data = await req<{ sessions: SessionSummary[] }>('/api/sessions')
  return data.sessions
}

export async function stopSession(sessionId: string): Promise<void> {
  await req(`/api/sessions/${sessionId}/stop`, { method: 'POST' })
}

export async function stepSession(sessionId: string): Promise<void> {
  await req(`/api/sessions/${sessionId}/step`, { method: 'POST' })
}

export async function resumeSession(sessionId: string): Promise<void> {
  await req(`/api/sessions/${sessionId}/resume`, { method: 'POST' })
}

export async function sendMessage(sessionId: string, text: string): Promise<void> {
  await req(`/api/sessions/${sessionId}/message`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  })
}

// apiKey is write-only end to end: the backend never echoes it back (see main.py's
// get_session/update_session_settings), so omit the field entirely rather than sending an
// empty string when the user hasn't typed a new one -- an empty string is a real instruction
// ("drop the override, fall back to the SDK's own credential resolution"), not a no-op.
export async function updateSessionSettings(
  sessionId: string,
  params: { model?: string; apiKey?: string; systemPrompt?: string },
): Promise<void> {
  await req(`/api/sessions/${sessionId}/settings`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
}

export function downloadValUrl(sessionId: string): string {
  return `/api/sessions/${sessionId}/download`
}
