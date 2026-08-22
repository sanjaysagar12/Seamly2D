import type { ModelOption, PieceInfo, SessionDetail, SessionSummary } from './types'

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

export async function renameMeasurement(filename: string, newFilename: string): Promise<string> {
  const data = await req<{ filename: string }>(`/api/measurements/${encodeURIComponent(filename)}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ newFilename }),
  })
  return data.filename
}

export async function deleteMeasurement(filename: string): Promise<void> {
  await req(`/api/measurements/${encodeURIComponent(filename)}`, { method: 'DELETE' })
}

export async function listPatterns(): Promise<string[]> {
  const data = await req<{ files: string[] }>('/api/patterns')
  return data.files
}

export async function uploadPattern(file: File): Promise<string> {
  const form = new FormData()
  form.append('file', file)
  const data = await req<{ filename: string }>('/api/patterns', {
    method: 'POST',
    body: form,
  })
  return data.filename
}

export async function renamePattern(filename: string, newFilename: string): Promise<string> {
  const data = await req<{ filename: string }>(`/api/patterns/${encodeURIComponent(filename)}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ newFilename }),
  })
  return data.filename
}

export async function deletePattern(filename: string): Promise<void> {
  await req(`/api/patterns/${encodeURIComponent(filename)}`, { method: 'DELETE' })
}

export async function listPatternPieces(filename: string, measurementsFilename?: string): Promise<PieceInfo[]> {
  // Many real multi-size pattern files reference a measurements file by whatever path
  // they were originally authored at, which never resolves once uploaded here -- passing
  // the measurement file selected alongside this pattern lets the backend override that
  // stale reference (same as starting a real session with both files does).
  const qs = measurementsFilename ? `?measurementsFilename=${encodeURIComponent(measurementsFilename)}` : ''
  const data = await req<{ pieces: PieceInfo[] }>(`/api/patterns/${encodeURIComponent(filename)}/pieces${qs}`)
  return data.pieces
}

export async function listSessionPieces(sessionId: string): Promise<PieceInfo[]> {
  const data = await req<{ pieces: PieceInfo[] }>(`/api/sessions/${sessionId}/pieces`)
  return data.pieces
}

export async function getPieceSnapshot(
  sessionId: string,
  piece: string,
): Promise<{ piece: string; url: string }> {
  return req(`/api/sessions/${sessionId}/pieces/${encodeURIComponent(piece)}/snapshot`)
}

export async function startSession(params: {
  goal: string
  measurementsFilename?: string
  patternFilename?: string
  focusPiece?: string
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

export async function deleteSession(sessionId: string): Promise<void> {
  await req(`/api/sessions/${sessionId}`, { method: 'DELETE' })
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
  params: { model?: string; apiKey?: string; systemPrompt?: string; goal?: string },
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
