import { useState } from 'react'
import { StartScreen } from './components/StartScreen'
import { LiveSession } from './components/LiveSession'

export default function App() {
  const [sessionId, setSessionId] = useState<string | null>(null)

  if (!sessionId) {
    return <StartScreen onStarted={setSessionId} />
  }

  return <LiveSession sessionId={sessionId} onNewSession={() => setSessionId(null)} />
}
