import { useEffect, useRef, useState } from 'react'
import {
  listMeasurements,
  listModels,
  listPatternPieces,
  listPatterns,
  listSessions,
  startSession,
  uploadMeasurement,
  uploadPattern,
} from '../lib/api'
import type { ModelOption, PieceInfo, SessionSummary } from '../lib/types'
import { STOP_REASON_LABELS } from '../lib/types'
import './StartScreen.css'

interface Props {
  onStarted: (sessionId: string) => void
}

function SessionRow({ session, onOpen }: { session: SessionSummary; onOpen: () => void }) {
  return (
    <button type="button" className="session-row" onClick={onOpen} title={session.goal}>
      <span className={`status-dot status-dot-${session.status}`} />
      <span className="session-row-info">
        <span className="session-row-goal">{session.goal || '(no goal set)'}</span>
        <span className="session-row-meta mono">
          step {session.step} · {session.model}
          {session.stopReason ? ` · ${STOP_REASON_LABELS[session.stopReason] ?? session.stopReason}` : ''}
        </span>
      </span>
    </button>
  )
}

export function StartScreen({ onStarted }: Props) {
  const [goal, setGoal] = useState('')
  const [measurements, setMeasurements] = useState<string[]>([])
  const [selected, setSelected] = useState<string>('')
  const [patterns, setPatterns] = useState<string[]>([])
  const [selectedPattern, setSelectedPattern] = useState<string>('')
  const [patternPieces, setPatternPieces] = useState<PieceInfo[]>([])
  const [piecesLoading, setPiecesLoading] = useState(false)
  const [focusPiece, setFocusPiece] = useState<string>('')
  const [models, setModels] = useState<ModelOption[]>([])
  const [selectedModel, setSelectedModel] = useState<string>('')
  const [systemPrompt, setSystemPrompt] = useState('')
  const [systemPromptOpen, setSystemPromptOpen] = useState(false)
  const [stepLimit, setStepLimit] = useState(60)
  const [autorun, setAutorun] = useState(true)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [sessions, setSessions] = useState<SessionSummary[]>([])
  const fileInputRef = useRef<HTMLInputElement>(null)
  const patternInputRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    listMeasurements()
      .then((files) => {
        setMeasurements(files)
        if (files.length > 0) setSelected(files[0])
      })
      .catch((err) => setError(String(err)))
  }, [])

  useEffect(() => {
    // Left unselected by default, unlike measurements -- omitting --pattern is a
    // supported, meaningful choice (actiond starts from an empty pattern), not just
    // "no file uploaded yet".
    listPatterns()
      .then((files) => setPatterns(files))
      .catch((err) => setError(String(err)))
  }, [])

  useEffect(() => {
    setFocusPiece('')
    if (!selectedPattern) {
      setPatternPieces([])
      return
    }
    let cancelled = false
    setPiecesLoading(true)
    // Pass along whichever measurement file is currently selected -- many real
    // multi-size patterns fail to load at all without their measurements (a stale
    // reference baked into the file itself, see api.ts's listPatternPieces comment),
    // so re-run this whenever either file selection changes, not just the pattern.
    listPatternPieces(selectedPattern, selected || undefined)
      .then((pieces) => {
        if (!cancelled) setPatternPieces(pieces)
      })
      .catch((err) => {
        if (!cancelled) {
          setPatternPieces([])
          setError(String(err))
        }
      })
      .finally(() => {
        if (!cancelled) setPiecesLoading(false)
      })
    return () => {
      cancelled = true
    }
  }, [selectedPattern, selected])

  useEffect(() => {
    listModels()
      .then((data) => {
        setModels(data.models)
        setSelectedModel(data.default && data.models.some((m) => m.id === data.default) ? data.default : data.models[0]?.id ?? '')
        setSystemPrompt(data.defaultSystemPrompt)
      })
      .catch((err) => setError(String(err)))
  }, [])

  useEffect(() => {
    let cancelled = false
    function refresh() {
      listSessions()
        .then((list) => {
          if (!cancelled) setSessions(list)
        })
        .catch(() => {})
    }
    refresh()
    const interval = setInterval(refresh, 3000)
    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [])

  async function handleUpload(file: File) {
    setBusy(true)
    setError(null)
    try {
      const filename = await uploadMeasurement(file)
      setMeasurements((prev) => (prev.includes(filename) ? prev : [...prev, filename]))
      setSelected(filename)
    } catch (err) {
      setError(String(err))
    } finally {
      setBusy(false)
    }
  }

  async function handleUploadPattern(file: File) {
    setBusy(true)
    setError(null)
    try {
      const filename = await uploadPattern(file)
      setPatterns((prev) => (prev.includes(filename) ? prev : [...prev, filename]))
      setSelectedPattern(filename)
    } catch (err) {
      setError(String(err))
    } finally {
      setBusy(false)
    }
  }

  async function handleStart() {
    // Goal is optional -- leaving it blank starts the session paused (see main.py's
    // start_session, which forces autorun off with no goal) so the first real
    // instruction can be given via the session page's chat box instead.
    setBusy(true)
    setError(null)
    try {
      const { sessionId } = await startSession({
        goal: goal.trim(),
        measurementsFilename: selected || undefined,
        patternFilename: selectedPattern || undefined,
        focusPiece: focusPiece || undefined,
        stepLimit,
        autorun,
        model: selectedModel || undefined,
        systemPrompt: systemPrompt.trim() || undefined,
      })
      onStarted(sessionId)
    } catch (err) {
      setError(String(err))
      setBusy(false)
    }
  }

  return (
    <div className="start-screen">
      <div className="start-layout">
        {sessions.length > 0 && (
          <div className="sessions-card">
            <div className="start-kicker">SESSIONS</div>
            <div className="session-list">
              {[...sessions].reverse().map((s) => (
                <SessionRow key={s.sessionId} session={s} onOpen={() => onStarted(s.sessionId)} />
              ))}
            </div>
          </div>
        )}
      <div className="start-card">
        <div className="start-kicker">SEAMLY2D &middot; ACTION AGENT</div>
        <h1>Draft a pattern by describing it.</h1>
        <p className="start-sub">
          An agent will pick one construction action at a time, look at the rendered result, and
          keep going until the piece is done &mdash; live, step by step, right here.
        </p>

        <label className="field-label" htmlFor="goal">
          Design goal (optional)
        </label>
        <textarea
          id="goal"
          className="goal-input"
          placeholder="e.g. Draft a basic bodice front block using these measurements, with a simple scoop neckline. Leave blank to describe it via chat once the session starts."
          value={goal}
          onChange={(e) => setGoal(e.target.value)}
          rows={4}
        />

        <div className="field-row">
          <div className="field-col">
            <label className="field-label" htmlFor="pattern">
              Base pattern (optional)
            </label>
            <select
              id="pattern"
              value={selectedPattern}
              onChange={(e) => setSelectedPattern(e.target.value)}
            >
              <option value="">Start from an empty pattern</option>
              {patterns.map((p) => (
                <option key={p} value={p}>
                  {p}
                </option>
              ))}
            </select>
            <button
              type="button"
              className="link-button"
              onClick={() => patternInputRef.current?.click()}
              disabled={busy}
            >
              + upload .val / .sm2d
            </button>
            <input
              ref={patternInputRef}
              type="file"
              accept=".val,.sm2d"
              style={{ display: 'none' }}
              onChange={(e) => {
                const file = e.target.files?.[0]
                if (file) void handleUploadPattern(file)
                e.target.value = ''
              }}
            />
            {piecesLoading && <div className="pieces-hint">Reading pieces…</div>}
            {!piecesLoading && patternPieces.length > 0 && (
              <div className="pieces-picker">
                <label className="field-label pieces-picker-label" htmlFor="focus-piece">
                  Focus piece ({patternPieces.length} found)
                </label>
                <select id="focus-piece" value={focusPiece} onChange={(e) => setFocusPiece(e.target.value)}>
                  <option value="">Whole pattern (no focus)</option>
                  {patternPieces.map((p) => (
                    <option key={p.id} value={p.name}>
                      {p.name}
                    </option>
                  ))}
                </select>
              </div>
            )}
          </div>

          <div className="field-col">
            <label className="field-label" htmlFor="measurements">
              Measurement file
            </label>
            <select
              id="measurements"
              value={selected}
              onChange={(e) => setSelected(e.target.value)}
              disabled={measurements.length === 0}
            >
              {measurements.length === 0 && <option value="">No files uploaded yet</option>}
              {measurements.map((m) => (
                <option key={m} value={m}>
                  {m}
                </option>
              ))}
            </select>
            <button
              type="button"
              className="link-button"
              onClick={() => fileInputRef.current?.click()}
              disabled={busy}
            >
              + upload .smis / .smms / .vst
            </button>
            <input
              ref={fileInputRef}
              type="file"
              accept=".smis,.smms,.vst"
              style={{ display: 'none' }}
              onChange={(e) => {
                const file = e.target.files?.[0]
                if (file) void handleUpload(file)
                e.target.value = ''
              }}
            />
          </div>
        </div>

        <div className="field-row">
          <div className="field-col field-col-model">
            <label className="field-label" htmlFor="model">
              Model
            </label>
            <select id="model" value={selectedModel} onChange={(e) => setSelectedModel(e.target.value)}>
              {models.map((m) => (
                <option key={m.id} value={m.id}>
                  {m.label}
                </option>
              ))}
            </select>
          </div>

          <div className="field-col field-col-narrow">
            <label className="field-label" htmlFor="step-limit">
              Step limit
            </label>
            <input
              id="step-limit"
              type="number"
              min={1}
              max={500}
              value={stepLimit}
              onChange={(e) => setStepLimit(Number(e.target.value))}
            />
          </div>
        </div>

        {goal.trim() ? (
          <label className="autorun-toggle">
            <input type="checkbox" checked={autorun} onChange={(e) => setAutorun(e.target.checked)} />
            Run automatically (uncheck to step through manually for debugging)
          </label>
        ) : (
          <div className="autorun-toggle autorun-toggle-hint">
            No goal yet -- the session will start paused. Send your first instruction from the chat
            box once it opens.
          </div>
        )}

        <button
          type="button"
          className="link-button system-prompt-toggle"
          onClick={() => setSystemPromptOpen((v) => !v)}
        >
          {systemPromptOpen ? '− hide system prompt' : '+ edit system prompt'}
        </button>
        {systemPromptOpen && (
          <div className="field-col field-col-wide">
            <label className="field-label" htmlFor="system-prompt">
              System prompt
            </label>
            <textarea
              id="system-prompt"
              className="system-prompt-input"
              value={systemPrompt}
              onChange={(e) => setSystemPrompt(e.target.value)}
              rows={10}
            />
            <button
              type="button"
              className="link-button"
              onClick={() => listModels().then((data) => setSystemPrompt(data.defaultSystemPrompt))}
            >
              reset to default
            </button>
          </div>
        )}

        {error && <div className="start-error">{error}</div>}

        <button className="start-button" onClick={handleStart} disabled={busy}>
          {busy ? 'Starting…' : 'Start session'}
        </button>
      </div>
      </div>
    </div>
  )
}
