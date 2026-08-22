import { useRef, useState } from 'react'
import './FilePicker.css'

interface Props {
  label: string
  files: string[]
  selected: string
  onSelect: (filename: string) => void
  onUpload: (file: File) => void | Promise<void>
  onRename: (oldName: string, newName: string) => void | Promise<void>
  onDelete: (filename: string) => void | Promise<void>
  accept: string
  uploadLabel: string
  emptyHint: string
  /** If set, an extra selectable row offering "no file" (value ''), e.g. "Start from an empty pattern". */
  noneLabel?: string
  disabled?: boolean
}

/** A small file manager: pick which uploaded file is active, upload a new one, rename
 * or delete an existing one -- shared between the home page's base-pattern and
 * measurement-file pickers, which were otherwise near-identical raw <select>s with no
 * way to fix a typo'd name or clear out an old upload. */
export function FilePicker({
  label,
  files,
  selected,
  onSelect,
  onUpload,
  onRename,
  onDelete,
  accept,
  uploadLabel,
  emptyHint,
  noneLabel,
  disabled,
}: Props) {
  const [renaming, setRenaming] = useState<string | null>(null)
  const [renameValue, setRenameValue] = useState('')
  const [busyFile, setBusyFile] = useState<string | null>(null)
  const inputRef = useRef<HTMLInputElement>(null)

  function startRename(filename: string) {
    setRenaming(filename)
    setRenameValue(filename)
  }

  async function commitRename(oldName: string) {
    const newName = renameValue.trim()
    setRenaming(null)
    if (!newName || newName === oldName) return
    setBusyFile(oldName)
    try {
      await onRename(oldName, newName)
    } finally {
      setBusyFile(null)
    }
  }

  async function handleDelete(filename: string) {
    if (!window.confirm(`Delete "${filename}"? This cannot be undone.`)) return
    setBusyFile(filename)
    try {
      await onDelete(filename)
    } finally {
      setBusyFile(null)
    }
  }

  return (
    <div className="file-picker">
      <div className="file-picker-label">{label}</div>

      <div className="file-picker-list">
        {noneLabel && (
          <button
            type="button"
            className={`file-picker-row ${selected === '' ? 'file-picker-row-active' : ''}`}
            onClick={() => onSelect('')}
          >
            <span className="file-picker-radio" />
            <span className="file-picker-name file-picker-name-muted">{noneLabel}</span>
          </button>
        )}

        {files.length === 0 && !noneLabel && <div className="file-picker-empty">{emptyHint}</div>}

        {files.map((filename) =>
          renaming === filename ? (
            <div key={filename} className="file-picker-row file-picker-row-editing">
              <input
                autoFocus
                className="file-picker-rename-input"
                value={renameValue}
                onChange={(e) => setRenameValue(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') void commitRename(filename)
                  if (e.key === 'Escape') setRenaming(null)
                }}
                onBlur={() => void commitRename(filename)}
              />
            </div>
          ) : (
            <div
              key={filename}
              className={`file-picker-row ${selected === filename ? 'file-picker-row-active' : ''}`}
            >
              <button type="button" className="file-picker-row-select" onClick={() => onSelect(filename)}>
                <span className="file-picker-radio" />
                <span className="file-picker-name" title={filename}>
                  {busyFile === filename ? 'Working…' : filename}
                </span>
              </button>
              <span className="file-picker-actions">
                <button
                  type="button"
                  className="file-picker-icon-btn"
                  title="Rename"
                  disabled={busyFile === filename}
                  onClick={() => startRename(filename)}
                >
                  ✎
                </button>
                <button
                  type="button"
                  className="file-picker-icon-btn file-picker-icon-btn-danger"
                  title="Delete"
                  disabled={busyFile === filename}
                  onClick={() => void handleDelete(filename)}
                >
                  🗑
                </button>
              </span>
            </div>
          ),
        )}
      </div>

      <button
        type="button"
        className="link-button"
        onClick={() => inputRef.current?.click()}
        disabled={disabled}
      >
        + {uploadLabel}
      </button>
      <input
        ref={inputRef}
        type="file"
        accept={accept}
        style={{ display: 'none' }}
        onChange={(e) => {
          const file = e.target.files?.[0]
          if (file) void onUpload(file)
          e.target.value = ''
        }}
      />
    </div>
  )
}
