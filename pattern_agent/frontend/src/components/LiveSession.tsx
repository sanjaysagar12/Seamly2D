import { useEffect, useMemo, useRef, useState } from 'react'
import {
  downloadValUrl,
  getSession,
  listModels,
  resumeSession,
  sendMessage,
  stepSession,
  stopSession,
  updateSessionSettings,
} from '../lib/api'
import { useSessionSocket } from '../lib/useSessionSocket'
import { STOP_REASON_LABELS, type ModelOption, type StepRecord } from '../lib/types'
import './LiveSession.css'

interface Props {
  sessionId: string
  onNewSession: () => void
}

function StatusPill({ status }: { status: string }) {
  return <span className={`status-pill status-${status}`}>{status}</span>
}

function formatValue(v: unknown): string {
  if (v === undefined || v === null) return '—'
  if (typeof v === 'string') return v
  try {
    return JSON.stringify(v, null, 2)
  } catch {
    return String(v)
  }
}

function StepEntry({
  record,
  active,
  onSelect,
}: {
  record: StepRecord
  active: boolean
  onSelect: () => void
}) {
  const isInitial = record.step === 0 && !record.action
  const success = record.result?.success

  return (
    <div className={`step-entry ${active ? 'step-entry-active' : ''}`} onClick={onSelect}>
      <div className="step-entry-header">
        <span className="step-badge">{isInitial ? 'start' : record.step}</span>
        {record.action && (
          <span className="step-op mono">
            {record.action.op}
            {success === false && <span className="step-fail-mark"> ✕</span>}
            {success === true && <span className="step-ok-mark"> ✓</span>}
          </span>
        )}
        {isInitial && <span className="step-op">Initial state</span>}
      </div>

      {record.thinking && <p className="step-thinking">{record.thinking}</p>}
      {record.text && <p className="step-text">{record.text}</p>}

      {record.action && (
        <pre className="step-json mono">{formatValue(record.action.input)}</pre>
      )}

      {record.result && !record.result.success && (
        <pre className="step-error mono">{formatValue(record.result.error)}</pre>
      )}

      {record.snapshotUrl && (
        <img className="step-thumb" src={record.snapshotUrl} alt={`Snapshot after step ${record.step}`} />
      )}
    </div>
  )
}

export function LiveSession({ sessionId, onNewSession }: Props) {
  const state = useSessionSocket(sessionId)
  const [goal, setGoal] = useState<string>('')
  const [model, setModel] = useState<string>('')
  const [stepLimit, setStepLimit] = useState<number>(0)
  const [selectedStep, setSelectedStep] = useState<number | null>(null)
  const [busy, setBusy] = useState(false)
  const [chatText, setChatText] = useState('')
  const [chatBusy, setChatBusy] = useState(false)
  const [chatError, setChatError] = useState<string | null>(null)
  const timelineRef = useRef<HTMLDivElement>(null)
  const autoScroll = useRef(true)

  // Runtime settings panel: change model / Anthropic API key / system prompt on the live
  // session without restarting it. apiKeyInput is deliberately never prefilled from the
  // server (see api.ts's updateSessionSettings comment -- it's write-only end to end).
  const [models, setModels] = useState<ModelOption[]>([])
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [systemPrompt, setSystemPrompt] = useState('')
  const [apiKeyInput, setApiKeyInput] = useState('')
  const [settingsBusy, setSettingsBusy] = useState(false)
  const [settingsError, setSettingsError] = useState<string | null>(null)
  const [settingsSaved, setSettingsSaved] = useState(false)

  useEffect(() => {
    getSession(sessionId)
      .then((detail) => {
        setGoal(detail.goal)
        setStepLimit(detail.stepLimit)
        setModel(detail.model)
        setSystemPrompt(detail.systemPrompt)
      })
      .catch(() => {})
  }, [sessionId])

  useEffect(() => {
    listModels()
      .then((data) => setModels(data.models))
      .catch(() => {})
  }, [])

  useEffect(() => {
    if (!autoScroll.current || !timelineRef.current) return
    timelineRef.current.scrollTop = timelineRef.current.scrollHeight
  }, [state.steps])

  const latestWithSnapshot = useMemo(
    () => [...state.steps].reverse().find((s) => s.snapshotUrl),
    [state.steps],
  )
  const displayed = selectedStep !== null ? state.steps.find((s) => s.step === selectedStep) : latestWithSnapshot

  const canStep = state.status === 'paused' || state.status === 'idle'
  const canRun = state.status === 'paused' || state.status === 'idle'
  const canStop = state.status === 'running' || state.status === 'paused'
  const terminal = state.status === 'complete' || state.status === 'error'
  // Chat can revive a paused, complete, or errored session (it (re)opens actiond from
  // the last save) -- only blocked while the loop is actively mid-turn.
  const canChat = state.status !== 'running'
  // Same reasoning as canChat: the backend refuses update_settings mid-turn too (swapping
  // the client/prompt out from under an in-flight streamed call is undefined), so mirror
  // that guard here rather than let the user hit a 409.
  const canEditSettings = state.status !== 'running'

  async function withBusy(fn: () => Promise<void>) {
    setBusy(true)
    try {
      await fn()
    } catch (err) {
      console.error(err)
    } finally {
      setBusy(false)
    }
  }

  async function handleSendMessage() {
    const text = chatText.trim()
    if (!text) return
    setChatBusy(true)
    setChatError(null)
    try {
      await sendMessage(sessionId, text)
      setChatText('')
      setSelectedStep(null) // jump back to latest so the new turns are visible
    } catch (err) {
      setChatError(String(err))
    } finally {
      setChatBusy(false)
    }
  }

  async function handleApplySettings() {
    setSettingsBusy(true)
    setSettingsError(null)
    setSettingsSaved(false)
    try {
      await updateSessionSettings(sessionId, {
        model,
        systemPrompt,
        // Omit entirely unless the user actually typed something -- see api.ts's comment on
        // why an empty string is not the same as "no change" here.
        ...(apiKeyInput.trim() ? { apiKey: apiKeyInput.trim() } : {}),
      })
      setApiKeyInput('') // never leave a typed secret sitting in the field longer than needed
      setSettingsSaved(true)
    } catch (err) {
      setSettingsError(String(err))
    } finally {
      setSettingsBusy(false)
    }
  }

  return (
    <div className="live-session">
      <header className="live-header">
        <button className="back-button" onClick={onNewSession} title="Start a new session">
          ← New session
        </button>
        <div className="live-header-goal">
          <div className="live-header-label">Goal</div>
          <div className="live-header-text">{goal || '…'}</div>
        </div>
        <div className="live-header-status">
          {model && <span className="model-badge mono">{model}</span>}
          <StatusPill status={state.status} />
          <span className="step-counter mono">
            step {Math.max(0, ...state.steps.map((s) => s.step))}
            {stepLimit ? ` / ${stepLimit}` : ''}
          </span>
          <button
            type="button"
            className={`settings-toggle ${settingsOpen ? 'settings-toggle-active' : ''}`}
            onClick={() => setSettingsOpen((v) => !v)}
            title="Change model, API key, or system prompt"
          >
            ⚙ Settings
          </button>
        </div>
      </header>

      {settingsOpen && (
        <div className="settings-panel">
          {!canEditSettings && (
            <div className="settings-hint">Wait for the current step to finish before changing settings.</div>
          )}
          <div className="settings-row">
            <div className="settings-col">
              <label className="field-label" htmlFor="settings-model">
                Model
              </label>
              <select
                id="settings-model"
                value={model}
                onChange={(e) => setModel(e.target.value)}
                disabled={!canEditSettings}
              >
                {models.map((m) => (
                  <option key={m.id} value={m.id}>
                    {m.label}
                  </option>
                ))}
              </select>
            </div>
            <div className="settings-col">
              <label className="field-label" htmlFor="settings-api-key">
                Anthropic API key
              </label>
              <input
                id="settings-api-key"
                type="password"
                autoComplete="off"
                placeholder="Leave blank to keep the current key"
                value={apiKeyInput}
                onChange={(e) => setApiKeyInput(e.target.value)}
                disabled={!canEditSettings}
              />
            </div>
          </div>
          <div className="settings-col settings-col-wide">
            <label className="field-label" htmlFor="settings-system-prompt">
              System prompt
            </label>
            <textarea
              id="settings-system-prompt"
              rows={8}
              value={systemPrompt}
              onChange={(e) => setSystemPrompt(e.target.value)}
              disabled={!canEditSettings}
            />
          </div>
          <div className="settings-actions">
            <button
              type="button"
              disabled={!canEditSettings || settingsBusy}
              onClick={() => void handleApplySettings()}
            >
              {settingsBusy ? 'Applying…' : 'Apply'}
            </button>
            {settingsSaved && <span className="settings-saved">Saved — takes effect next turn.</span>}
            {settingsError && <span className="settings-error">{settingsError}</span>}
          </div>
        </div>
      )}

      {state.stopReason && (
        <div className={`stop-banner stop-banner-${state.status}`}>
          {STOP_REASON_LABELS[state.stopReason] ?? state.stopReason}
          {state.finalSummary && <span className="stop-summary"> — {state.finalSummary}</span>}
        </div>
      )}

      {state.errors
        .filter((e) => e.fatal)
        .slice(-1)
        .map((e, i) => (
          <div key={i} className="error-banner">
            {e.message}
          </div>
        ))}

      <div className="live-body">
        <div className="snapshot-panel">
          <div className="snapshot-frame">
            {displayed?.snapshotUrl ? (
              <img src={displayed.snapshotUrl} alt={`Pattern snapshot at step ${displayed.step}`} />
            ) : (
              <div className="snapshot-placeholder">No snapshot yet</div>
            )}
          </div>
          <div className="snapshot-caption mono">
            {displayed ? `step ${displayed.step}` : ''}
            {selectedStep !== null && (
              <button className="link-button" onClick={() => setSelectedStep(null)}>
                jump to latest
              </button>
            )}
          </div>

          <div className="controls">
            <button
              disabled={busy || !canStep}
              onClick={() => withBusy(() => stepSession(sessionId))}
            >
              Step once
            </button>
            <button
              disabled={busy || !canRun}
              onClick={() => withBusy(() => resumeSession(sessionId))}
            >
              Run continuously
            </button>
            <button
              className="danger"
              disabled={busy || !canStop}
              onClick={() => withBusy(() => stopSession(sessionId))}
            >
              Stop
            </button>
            {terminal && state.valUrl && (
              <a className="download-button" href={downloadValUrl(sessionId)} download>
                Download .val
              </a>
            )}
          </div>
        </div>

        <div className="timeline-column">
          <div
            className="timeline"
            ref={timelineRef}
            onScroll={(e) => {
              const el = e.currentTarget
              autoScroll.current = el.scrollHeight - el.scrollTop - el.clientHeight < 60
            }}
          >
            {state.steps.length === 0 && state.chatMessages.length === 0 && (
              <div className="timeline-empty">Waiting for the agent…</div>
            )}
            {state.steps.map((record) => (
              <div key={record.step}>
                {state.chatMessages
                  .filter((m) => m.afterStep === record.step)
                  .map((m) => (
                    <div key={m.id} className="chat-bubble">
                      {m.text}
                    </div>
                  ))}
                <StepEntry
                  record={record}
                  active={selectedStep === record.step}
                  onSelect={() => setSelectedStep(record.step)}
                />
              </div>
            ))}
            {state.chatMessages
              .filter((m) => !state.steps.some((s) => s.step === m.afterStep))
              .map((m) => (
                <div key={m.id} className="chat-bubble">
                  {m.text}
                </div>
              ))}
          </div>

          <form
            className="chat-composer"
            onSubmit={(e) => {
              e.preventDefault()
              void handleSendMessage()
            }}
          >
            <input
              type="text"
              placeholder={
                canChat ? 'Ask the agent to edit the pattern…' : 'Wait for the current step to finish…'
              }
              value={chatText}
              onChange={(e) => setChatText(e.target.value)}
              disabled={!canChat || chatBusy}
            />
            <button type="submit" disabled={!canChat || chatBusy || !chatText.trim()}>
              {chatBusy ? '…' : 'Send'}
            </button>
          </form>
          {chatError && <div className="chat-error">{chatError}</div>}
        </div>
      </div>
    </div>
  )
}
