import { useEffect, useState } from 'react'
import {
  deleteMeasurement,
  deletePattern,
  deleteSession,
  listMeasurements,
  listModels,
  listPatternPieces,
  listPatterns,
  listSessions,
  renameMeasurement,
  renamePattern,
  startSession,
  uploadMeasurement,
  uploadPattern,
} from '../lib/api'
import { FilePicker } from './FilePicker'
import type { ModelOption, PieceInfo, SessionSummary } from '../lib/types'
import { STOP_REASON_LABELS } from '../lib/types'
import './StartScreen.css'

interface Props {
  onStarted: (sessionId: string) => void
}

function SessionRow({
  session,
  onOpen,
  onDelete,
}: {
  session: SessionSummary
  onOpen: () => void
  onDelete: () => void
}) {
  return (
    <div className="session-row">
      <button type="button" className="session-row-main" onClick={onOpen} title={session.goal}>
        <span className={`status-dot status-dot-${session.status}`} />
        <span className="session-row-info">
          <span className="session-row-goal">{session.goal || '(no goal set)'}</span>
          <span className="session-row-meta mono">
            step {session.step} · {session.model}
            {session.stopReason ? ` · ${STOP_REASON_LABELS[session.stopReason] ?? session.stopReason}` : ''}
          </span>
        </span>
      </button>
      <button
        type="button"
        className="session-row-delete"
        title="Delete session"
        onClick={(e) => {
          e.stopPropagation()
          onDelete()
        }}
      >
        🗑
      </button>
    </div>
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

  async function handleUploadMeasurement(file: File) {
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

  async function handleRenameMeasurement(oldName: string, newName: string) {
    try {
      const filename = await renameMeasurement(oldName, newName)
      setMeasurements((prev) => prev.map((m) => (m === oldName ? filename : m)))
      if (selected === oldName) setSelected(filename)
    } catch (err) {
      setError(String(err))
    }
  }

  async function handleDeleteMeasurement(filename: string) {
    try {
      await deleteMeasurement(filename)
      setMeasurements((prev) => prev.filter((m) => m !== filename))
      if (selected === filename) setSelected('')
    } catch (err) {
      setError(String(err))
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

  async function handleRenamePattern(oldName: string, newName: string) {
    try {
      const filename = await renamePattern(oldName, newName)
      setPatterns((prev) => prev.map((p) => (p === oldName ? filename : p)))
      if (selectedPattern === oldName) setSelectedPattern(filename)
    } catch (err) {
      setError(String(err))
    }
  }

  async function handleDeletePattern(filename: string) {
    try {
      await deletePattern(filename)
      setPatterns((prev) => prev.filter((p) => p !== filename))
      if (selectedPattern === filename) setSelectedPattern('')
    } catch (err) {
      setError(String(err))
    }
  }

  async function handleDeleteSession(sessionId: string) {
    if (!window.confirm('Delete this session? This cannot be undone.')) return
    try {
      await deleteSession(sessionId)
      setSessions((prev) => prev.filter((s) => s.sessionId !== sessionId))
    } catch (err) {
      setError(String(err))
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
                <SessionRow
                  key={s.sessionId}
                  session={s}
                  onOpen={() => onStarted(s.sessionId)}
                  onDelete={() => void handleDeleteSession(s.sessionId)}
                />
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

          <section className="start-section">
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
          </section>

          <section className="start-section source-section">
            <div className="source-section-title">Source files</div>
            <div className="field-row">
              <div className="field-col">
                <FilePicker
                  label="Base pattern (optional)"
                  files={patterns}
                  selected={selectedPattern}
                  onSelect={setSelectedPattern}
                  onUpload={handleUploadPattern}
                  onRename={handleRenamePattern}
                  onDelete={handleDeletePattern}
                  accept=".val,.sm2d"
                  uploadLabel="upload .val / .sm2d"
                  emptyHint="No pattern files uploaded yet."
                  noneLabel="Start from an empty pattern"
                  disabled={busy}
                />

                {piecesLoading && (
                  <div className="pieces-hint">
                    <span className="spinner" /> Reading pieces…
                  </div>
                )}
                {!piecesLoading && patternPieces.length > 0 && (
                  <div className="pieces-picker">
                    <div className="field-label pieces-picker-label">
                      Focus piece &middot; {patternPieces.length} found
                    </div>
                    <div className="piece-chip-row">
                      <button
                        type="button"
                        className={`piece-chip ${focusPiece === '' ? 'piece-chip-active' : ''}`}
                        onClick={() => setFocusPiece('')}
                      >
                        Whole pattern
                      </button>
                      {patternPieces.map((p) => (
                        <button
                          type="button"
                          key={p.id}
                          className={`piece-chip ${focusPiece === p.name ? 'piece-chip-active' : ''}`}
                          onClick={() => setFocusPiece(p.name)}
                        >
                          {p.name}
                        </button>
                      ))}
                    </div>
                  </div>
                )}
              </div>

              <div className="field-col">
                <FilePicker
                  label="Measurement file"
                  files={measurements}
                  selected={selected}
                  onSelect={setSelected}
                  onUpload={handleUploadMeasurement}
                  onRename={handleRenameMeasurement}
                  onDelete={handleDeleteMeasurement}
                  accept=".smis,.smms,.vst"
                  uploadLabel="upload .smis / .smms / .vst"
                  emptyHint="No measurement files uploaded yet."
                  noneLabel="No measurements"
                  disabled={busy}
                />
              </div>
            </div>
          </section>

          <section className="start-section">
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
              <div className="hint-box">
                No goal yet &mdash; the session will start paused. Send your first instruction from the
                chat box once it opens.
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
          </section>

          {error && <div className="start-error">{error}</div>}

          <button className="start-button" onClick={handleStart} disabled={busy}>
            {busy ? 'Starting…' : 'Start session'}
          </button>
        </div>
      </div>
    </div>
  )
}
