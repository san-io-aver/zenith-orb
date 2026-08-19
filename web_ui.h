/*
 * web_ui.h  — Embedded HTML/CSS/JS Dashboard
 * Served from ESP32 flash via send_P()
 * Do NOT exceed ~50 KB or you may hit heap limits on ESP32-C3.
 */

#pragma once
#include <pgmspace.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ORB DISPLAY — Mission Control</title>
<meta name="description" content="ESP32 OLED Orb Display mission control dashboard. Control solar system orrery, satellite radar, and custom text HUD.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&family=Share+Tech+Mono&display=swap" rel="stylesheet">
<style>
  /* ══════════════════════════════════════════════════════
     DESIGN TOKENS
  ══════════════════════════════════════════════════════ */
  :root {
    --c-bg:        #04060d;
    --c-surface:   #0a0f1e;
    --c-surface2:  #0f1829;
    --c-border:    #1a2a45;
    --c-accent:    #00d4ff;
    --c-accent2:   #7b2fff;
    --c-accent3:   #ff6b35;
    --c-success:   #00ff9d;
    --c-danger:    #ff3860;
    --c-text:      #c8d8f0;
    --c-muted:     #4a6080;
    --c-glow:      rgba(0,212,255,.18);
    --c-glow2:     rgba(123,47,255,.14);
    --r-card:      14px;
    --font-ui:     'Outfit', sans-serif;
    --font-mono:   'Share Tech Mono', monospace;
    --trans:       .2s cubic-bezier(.4,0,.2,1);
  }

  /* ══════════════════════════════════════════════════════
     RESET & BASE
  ══════════════════════════════════════════════════════ */
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  html { color-scheme: dark; }

  body {
    font-family: var(--font-ui);
    background: var(--c-bg);
    color: var(--c-text);
    min-height: 100vh;
    overflow-x: hidden;
  }

  /* Animated grid background */
  body::before {
    content: '';
    position: fixed; inset: 0; z-index: 0; pointer-events: none;
    background-image:
      linear-gradient(rgba(0,212,255,.03) 1px, transparent 1px),
      linear-gradient(90deg, rgba(0,212,255,.03) 1px, transparent 1px);
    background-size: 40px 40px;
  }

  /* Ambient orb glow */
  body::after {
    content: '';
    position: fixed; top: -30%; left: 50%; translate: -50% 0;
    width: 80vw; height: 80vw; max-width: 800px; max-height: 800px;
    border-radius: 50%;
    background: radial-gradient(circle, rgba(123,47,255,.08) 0%, transparent 70%);
    pointer-events: none; z-index: 0;
  }

  /* ══════════════════════════════════════════════════════
     LAYOUT
  ══════════════════════════════════════════════════════ */
  .wrapper {
    position: relative; z-index: 1;
    max-width: 1100px; margin: 0 auto;
    padding: 24px 16px 60px;
  }

  /* ══════════════════════════════════════════════════════
     HEADER
  ══════════════════════════════════════════════════════ */
  .site-header {
    display: flex; align-items: center; gap: 16px;
    margin-bottom: 32px;
    padding-bottom: 20px;
    border-bottom: 1px solid var(--c-border);
  }

  .orb-icon {
    width: 48px; height: 48px;
    border-radius: 50%;
    background: conic-gradient(from 0deg, var(--c-accent), var(--c-accent2), var(--c-accent));
    box-shadow: 0 0 24px var(--c-glow), 0 0 48px var(--c-glow2);
    flex-shrink: 0;
    animation: spin 12s linear infinite;
  }

  @keyframes spin { to { rotate: 360deg; } }

  .site-header h1 {
    font-size: clamp(1.2rem, 4vw, 1.8rem);
    font-weight: 700;
    letter-spacing: .08em;
    background: linear-gradient(135deg in oklab, var(--c-accent), var(--c-accent2));
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
  }

  .site-header p {
    font-family: var(--font-mono);
    font-size: .72rem;
    color: var(--c-muted);
    margin-top: 2px;
  }

  .status-bar {
    margin-left: auto;
    display: flex; gap: 12px; flex-wrap: wrap; justify-content: flex-end;
  }

  .status-chip {
    font-family: var(--font-mono);
    font-size: .68rem;
    padding: 4px 10px;
    border-radius: 20px;
    border: 1px solid var(--c-border);
    background: var(--c-surface);
    color: var(--c-muted);
    transition: color var(--trans), border-color var(--trans);
  }

  .status-chip.ok { color: var(--c-success); border-color: var(--c-success); }
  .status-chip.err { color: var(--c-danger); border-color: var(--c-danger); }

  /* ══════════════════════════════════════════════════════
     TABS
  ══════════════════════════════════════════════════════ */
  .tab-bar {
    display: flex; gap: 4px;
    border-bottom: 1px solid var(--c-border);
    margin-bottom: 28px;
    overflow-x: auto;
    scrollbar-width: none;
  }
  .tab-bar::-webkit-scrollbar { display: none; }

  .tab-btn {
    font-family: var(--font-ui);
    font-size: .82rem;
    font-weight: 600;
    letter-spacing: .06em;
    text-transform: uppercase;
    padding: 10px 20px;
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    color: var(--c-muted);
    cursor: pointer;
    white-space: nowrap;
    translate: 0 1px;
    transition: color var(--trans), border-color var(--trans);
  }

  .tab-btn:hover { color: var(--c-text); }

  .tab-btn[aria-selected="true"] {
    color: var(--c-accent);
    border-bottom-color: var(--c-accent);
  }

  .tab-panel { display: none; animation: fadeIn .25s ease; }
  .tab-panel.active { display: block; }

  @keyframes fadeIn { from { opacity: 0; translate: 0 8px; } to { opacity: 1; } }

  /* ══════════════════════════════════════════════════════
     CARDS
  ══════════════════════════════════════════════════════ */
  .card {
    background: var(--c-surface);
    border: 1px solid var(--c-border);
    border-radius: var(--r-card);
    padding: 22px 24px;
    margin-bottom: 16px;
    position: relative;
    overflow: hidden;
    transition: border-color var(--trans), box-shadow var(--trans);
  }

  .card::before {
    content: '';
    position: absolute; top: 0; left: 0; right: 0; height: 1px;
    background: linear-gradient(90deg in oklab,
      transparent, var(--c-accent), transparent);
    opacity: .4;
  }

  .card:hover {
    border-color: rgba(0,212,255,.25);
    box-shadow: 0 4px 30px rgba(0,212,255,.06);
  }

  .card-title {
    font-size: .72rem;
    font-weight: 600;
    letter-spacing: .12em;
    text-transform: uppercase;
    color: var(--c-accent);
    margin-bottom: 18px;
    display: flex; align-items: center; gap: 8px;
  }

  .card-title::before {
    content: '';
    display: block;
    width: 3px; height: 14px;
    border-radius: 2px;
    background: var(--c-accent);
    box-shadow: 0 0 8px var(--c-accent);
  }

  /* ══════════════════════════════════════════════════════
     MODE SELECTOR GRID
  ══════════════════════════════════════════════════════ */
  .mode-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 12px;
  }

  .mode-card {
    border: 1px solid var(--c-border);
    border-radius: 10px;
    padding: 18px 16px;
    cursor: pointer;
    background: var(--c-surface2);
    transition: all var(--trans);
    position: relative;
    overflow: hidden;
    text-align: center;
  }

  .mode-card::after {
    content: '';
    position: absolute; inset: 0;
    border-radius: 10px;
    background: radial-gradient(circle at 50% 0%, var(--c-glow), transparent 70%);
    opacity: 0;
    transition: opacity var(--trans);
  }

  .mode-card:hover::after, .mode-card.active::after { opacity: 1; }

  .mode-card.active {
    border-color: var(--c-accent);
    box-shadow: 0 0 20px var(--c-glow), inset 0 0 20px rgba(0,212,255,.04);
  }

  .mode-card input[type="radio"] { display: none; }

  .mode-icon {
    font-size: 2.2rem;
    display: block;
    margin-bottom: 8px;
    filter: drop-shadow(0 0 6px var(--c-accent));
  }

  .mode-name {
    font-size: .85rem;
    font-weight: 700;
    letter-spacing: .08em;
    color: var(--c-text);
  }

  .mode-desc {
    font-size: .72rem;
    color: var(--c-muted);
    margin-top: 4px;
  }

  /* ══════════════════════════════════════════════════════
     SLIDERS
  ══════════════════════════════════════════════════════ */
  .slider-row {
    display: grid;
    grid-template-columns: 160px 1fr 60px;
    align-items: center;
    gap: 16px;
    margin-bottom: 16px;
  }

  .slider-row:last-child { margin-bottom: 0; }

  .slider-label {
    font-family: var(--font-mono);
    font-size: .75rem;
    color: var(--c-muted);
  }

  input[type="range"] {
    -webkit-appearance: none;
    appearance: none;
    width: 100%; height: 4px;
    border-radius: 2px;
    background: var(--c-border);
    outline: none;
    cursor: pointer;
  }

  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 16px; height: 16px;
    border-radius: 50%;
    background: var(--c-accent);
    box-shadow: 0 0 8px var(--c-accent);
    cursor: pointer;
    transition: transform var(--trans);
  }

  input[type="range"]::-webkit-slider-thumb:hover { transform: scale(1.2); }

  input[type="range"]::-moz-range-thumb {
    width: 16px; height: 16px;
    border-radius: 50%;
    background: var(--c-accent);
    box-shadow: 0 0 8px var(--c-accent);
    border: none;
    cursor: pointer;
  }

  .slider-val {
    font-family: var(--font-mono);
    font-size: .82rem;
    color: var(--c-accent);
    text-align: right;
  }

  /* ══════════════════════════════════════════════════════
     BUTTONS
  ══════════════════════════════════════════════════════ */
  .btn {
    font-family: var(--font-ui);
    font-size: .82rem;
    font-weight: 600;
    letter-spacing: .06em;
    text-transform: uppercase;
    padding: 10px 22px;
    border-radius: 8px;
    border: none;
    cursor: pointer;
    transition: all var(--trans);
    display: inline-flex; align-items: center; gap: 6px;
  }

  .btn-primary {
    background: linear-gradient(135deg in oklab, var(--c-accent), var(--c-accent2));
    color: #000;
    box-shadow: 0 0 16px var(--c-glow);
  }

  .btn-primary:hover {
    transform: translateY(-2px);
    box-shadow: 0 4px 24px var(--c-glow);
  }

  .btn-primary:active { transform: translateY(0); }

  .btn-secondary {
    background: var(--c-surface2);
    color: var(--c-text);
    border: 1px solid var(--c-border);
  }

  .btn-secondary:hover {
    border-color: var(--c-accent);
    color: var(--c-accent);
  }

  .btn-danger {
    background: rgba(255,56,96,.1);
    color: var(--c-danger);
    border: 1px solid rgba(255,56,96,.3);
  }

  .btn-danger:hover {
    background: rgba(255,56,96,.2);
    box-shadow: 0 0 16px rgba(255,56,96,.2);
  }

  .btn-row {
    display: flex; gap: 10px; flex-wrap: wrap;
    margin-top: 20px;
  }

  /* ══════════════════════════════════════════════════════
     SELECT / RADIO GROUP
  ══════════════════════════════════════════════════════ */
  .radio-group {
    display: flex; gap: 8px; flex-wrap: wrap;
  }

  .radio-pill {
    position: relative;
  }

  .radio-pill input[type="radio"] {
    position: absolute; opacity: 0; width: 0; height: 0;
  }

  .radio-pill label {
    display: inline-block;
    padding: 8px 18px;
    border-radius: 20px;
    border: 1px solid var(--c-border);
    background: var(--c-surface2);
    font-size: .78rem;
    font-weight: 600;
    letter-spacing: .06em;
    color: var(--c-muted);
    cursor: pointer;
    transition: all var(--trans);
  }

  .radio-pill input[type="radio"]:checked + label {
    background: rgba(0,212,255,.12);
    border-color: var(--c-accent);
    color: var(--c-accent);
    box-shadow: 0 0 12px var(--c-glow);
  }

  .radio-pill label:hover { border-color: var(--c-accent); color: var(--c-text); }

  /* ══════════════════════════════════════════════════════
     TEXT INPUT
  ══════════════════════════════════════════════════════ */
  .text-field {
    width: 100%;
    background: var(--c-surface2);
    border: 1px solid var(--c-border);
    border-radius: 8px;
    padding: 12px 16px;
    color: var(--c-text);
    font-family: var(--font-mono);
    font-size: .88rem;
    outline: none;
    transition: border-color var(--trans), box-shadow var(--trans);
    margin-bottom: 12px;
  }

  .text-field:focus {
    border-color: var(--c-accent);
    box-shadow: 0 0 0 3px rgba(0,212,255,.1);
  }

  /* ══════════════════════════════════════════════════════
     TELEMETRY PANEL
  ══════════════════════════════════════════════════════ */
  .telem-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 12px;
  }

  .telem-item {
    background: var(--c-surface2);
    border: 1px solid var(--c-border);
    border-radius: 8px;
    padding: 14px 16px;
  }

  .telem-key {
    font-family: var(--font-mono);
    font-size: .62rem;
    color: var(--c-muted);
    letter-spacing: .1em;
    text-transform: uppercase;
    margin-bottom: 4px;
  }

  .telem-val {
    font-family: var(--font-mono);
    font-size: .92rem;
    color: var(--c-accent);
    word-break: break-all;
  }

  /* ══════════════════════════════════════════════════════
     TOAST NOTIFICATIONS
  ══════════════════════════════════════════════════════ */
  #toast-container {
    position: fixed; bottom: 24px; right: 24px;
    display: flex; flex-direction: column; gap: 8px;
    z-index: 9999;
  }

  .toast {
    font-family: var(--font-mono);
    font-size: .78rem;
    padding: 10px 18px;
    border-radius: 8px;
    border: 1px solid var(--c-border);
    background: var(--c-surface);
    backdrop-filter: blur(12px);
    animation: slideIn .3s cubic-bezier(.4,0,.2,1);
    box-shadow: 0 4px 20px rgba(0,0,0,.4);
  }

  .toast.ok  { border-color: var(--c-success); color: var(--c-success); }
  .toast.err { border-color: var(--c-danger);  color: var(--c-danger); }

  @keyframes slideIn {
    from { opacity: 0; translate: 20px 0; }
    to   { opacity: 1; }
  }

  /* ══════════════════════════════════════════════════════
     OLED PREVIEW CANVAS
  ══════════════════════════════════════════════════════ */
  .preview-wrap {
    display: flex; justify-content: center;
    margin: 16px 0;
  }

  #oled-preview {
    border: 2px solid var(--c-border);
    border-radius: 8px;
    background: #000;
    image-rendering: pixelated;
    box-shadow: 0 0 30px rgba(0,212,255,.1), inset 0 0 40px rgba(0,0,0,.8);
    width: min(512px, 100%);
    aspect-ratio: 2 / 1;
  }

  /* ══════════════════════════════════════════════════════
     SPEED WARP DISPLAY
  ══════════════════════════════════════════════════════ */
  .warp-display {
    font-family: var(--font-mono);
    font-size: 2.5rem;
    color: var(--c-accent);
    text-align: center;
    text-shadow: 0 0 20px var(--c-accent);
    margin: 8px 0;
    letter-spacing: .08em;
  }

  /* ══════════════════════════════════════════════════════
     DIVIDER
  ══════════════════════════════════════════════════════ */
  .divider {
    border: none;
    border-top: 1px solid var(--c-border);
    margin: 20px 0;
  }

  /* ══════════════════════════════════════════════════════
     RESPONSIVE
  ══════════════════════════════════════════════════════ */
  @media (max-width: 540px) {
    .slider-row { grid-template-columns: 120px 1fr 50px; gap: 10px; }
    .site-header { flex-wrap: wrap; }
    .status-bar { margin-left: 0; justify-content: flex-start; }
  }

  @media (prefers-reduced-motion: reduce) {
    .orb-icon { animation: none; }
    .tab-panel { animation: none; }
    .toast { animation: none; }
    * { transition-duration: 0.01ms !important; }
  }
</style>
</head>
<body>

<div id="toast-container" role="status" aria-live="polite" aria-atomic="true"></div>

<div class="wrapper">

  <!-- ═══ HEADER ═══ -->
  <header class="site-header">
    <div class="orb-icon" aria-hidden="true"></div>
    <div>
      <h1>ORB DISPLAY</h1>
      <p id="hdr-ip">Connecting to device...</p>
    </div>
    <div class="status-bar">
      <span id="chip-time"  class="status-chip">NTP —</span>
      <span id="chip-wifi"  class="status-chip">WiFi —</span>
      <span id="chip-heap"  class="status-chip">HEAP —</span>
      <span id="chip-tle0"  class="status-chip">ISS —</span>
    </div>
  </header>

  <!-- ═══ TABS ═══ -->
  <nav class="tab-bar" role="tablist" aria-label="Control panels">
    <button id="tab-mode"  class="tab-btn" role="tab" aria-selected="true"  aria-controls="panel-mode">MODE SELECT</button>
    <button id="tab-cal"   class="tab-btn" role="tab" aria-selected="false" aria-controls="panel-cal">CALIBRATION</button>
    <button id="tab-solar" class="tab-btn" role="tab" aria-selected="false" aria-controls="panel-solar">SOLAR SYSTEM</button>
    <button id="tab-radar" class="tab-btn" role="tab" aria-selected="false" aria-controls="panel-radar">RADAR</button>
    <button id="tab-text"  class="tab-btn" role="tab" aria-selected="false" aria-controls="panel-text">CUSTOM TEXT</button>
    <button id="tab-telem" class="tab-btn" role="tab" aria-selected="false" aria-controls="panel-telem">TELEMETRY</button>
  </nav>

  <!-- ═══════════════════════════════════════════════
       PANEL: MODE SELECT
  ═══════════════════════════════════════════════════ -->
  <section id="panel-mode" class="tab-panel active" role="tabpanel" aria-labelledby="tab-mode">

    <div class="card">
      <div class="card-title">Display Mode</div>
      <div class="mode-grid">
        <label id="mode-card-0" class="mode-card" for="mode-radio-0">
          <input type="radio" name="mode" id="mode-radio-0" value="0">
          <span class="mode-icon">🪐</span>
          <div class="mode-name">SOLAR ORRERY</div>
          <div class="mode-desc">Keplerian real-time planet positions with animated Sun</div>
        </label>
        <label id="mode-card-1" class="mode-card" for="mode-radio-1">
          <input type="radio" name="mode" id="mode-radio-1" value="1">
          <span class="mode-icon">📡</span>
          <div class="mode-name">SATELLITE RADAR</div>
          <div class="mode-desc">Live TLE orbital radar with sweep trail</div>
        </label>
        <label id="mode-card-2" class="mode-card" for="mode-radio-2">
          <input type="radio" name="mode" id="mode-radio-2" value="2">
          <span class="mode-icon">💬</span>
          <div class="mode-name">CUSTOM HUD</div>
          <div class="mode-desc">Scrolling vector text inside the lens viewport</div>
        </label>
        <label id="mode-card-3" class="mode-card" for="mode-radio-3" onclick="setMode(3)">
          <input type="radio" name="mode" id="mode-radio-3" value="3">
          <span class="mode-icon">🎱</span>
          <div class="mode-name">MAGIC 8 BALL</div>
          <div class="mode-desc">Ask a question and shake for guidance</div>
        
        </label>
      </div>
      
    </div>

  </section>

  <!-- ═══════════════════════════════════════════════
       PANEL: CALIBRATION
  ═══════════════════════════════════════════════════ -->
  <section id="panel-cal" class="tab-panel" role="tabpanel" aria-labelledby="tab-cal">

    <div class="card">
      <div class="card-title">Viewport Calibration</div>
      <p style="font-size:.78rem;color:var(--c-muted);margin-bottom:20px">
        Adjust the optical center of the physical lens relative to the OLED panel.
        Changes are applied live. Press "Save to EEPROM" to persist across reboots.
      </p>

      <div class="slider-row">
        <span class="slider-label">Center X</span>
        <input type="range" id="sl-cx" min="0" max="128" step="1" value="64" aria-label="Center X offset">
        <span class="slider-val" id="val-cx">64</span>
      </div>
      <div class="slider-row">
        <span class="slider-label">Center Y</span>
        <input type="range" id="sl-cy" min="0" max="64" step="1" value="32" aria-label="Center Y offset">
        <span class="slider-val" id="val-cy">32</span>
      </div>
      <div class="slider-row">
        <span class="slider-label">Viewport Radius</span>
        <input type="range" id="sl-r" min="10" max="64" step="1" value="28" aria-label="Viewport radius">
        <span class="slider-val" id="val-r">28</span>
      </div>

      <div class="btn-row">
        <button id="btn-save-cal" class="btn btn-primary" onclick="saveCalibration()">
          💾 Save to EEPROM
        </button>
        <button class="btn btn-secondary" onclick="loadStatus()">↺ Refresh</button>
      </div>
    </div>

    <div class="card">
      <div class="card-title">OLED Viewport Preview</div>
      <p style="font-size:.72rem;color:var(--c-muted);margin-bottom:12px">
        Visual representation of the circular viewport on the 128×64 panel.
      </p>
      <div class="preview-wrap">
        <canvas id="oled-preview" width="512" height="256" aria-label="OLED viewport preview"></canvas>
      </div>
    </div>

  </section>

  <!-- ═══════════════════════════════════════════════
       PANEL: SOLAR SYSTEM
  ═══════════════════════════════════════════════════ -->
  <section id="panel-solar" class="tab-panel" role="tabpanel" aria-labelledby="tab-solar">

    <div class="card">
      <div class="card-title">Speed Warp Control</div>
      <p style="font-size:.78rem;color:var(--c-muted);margin-bottom:20px">
        Accelerate the solar system simulation. 1× = real-time orbital motion.
      </p>

      <div class="warp-display">
        <span id="warp-display">100</span>×
      </div>
      <p style="text-align:center;font-size:.68rem;color:var(--c-muted);margin-bottom:16px">WARP FACTOR</p>

      <div class="slider-row">
        <span class="slider-label">Speed</span>
        <input type="range" id="sl-speed" min="1" max="50000" step="1" value="100"
               aria-label="Speed warp multiplier">
        <span class="slider-val" id="val-speed">100×</span>
      </div>

      <div class="btn-row">
        <button class="btn btn-secondary" onclick="setSpeed(1)">1× Real-time</button>
        <button class="btn btn-secondary" onclick="setSpeed(100)">100× Fast</button>
        <button class="btn btn-secondary" onclick="setSpeed(1000)">1K× Clockwork</button>
        <button class="btn btn-secondary" onclick="setSpeed(10000)">10K× Hyper</button>
        <button class="btn btn-primary" onclick="saveSpeed()">▶ Apply</button>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Planet Information</div>
      <div class="telem-grid">
        <div class="telem-item">
          <div class="telem-key">Mercury Period</div>
          <div class="telem-val">87.97 days</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Venus Period</div>
          <div class="telem-val">224.70 days</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Earth Period</div>
          <div class="telem-val">365.25 days</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Mars Period</div>
          <div class="telem-val">686.97 days</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Epoch</div>
          <div class="telem-val">J2000.0</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">UTC Time</div>
          <div class="telem-val" id="utc-time-solar">—</div>
        </div>
      </div>
    </div>

  </section>

  <!-- ═══════════════════════════════════════════════
       PANEL: RADAR
  ═══════════════════════════════════════════════════ -->
  <section id="panel-radar" class="tab-panel" role="tabpanel" aria-labelledby="tab-radar">

    <div class="card">
      <div class="card-title">Radar Target Selection</div>
      <div class="radio-group" id="radar-target-group">
        <div class="radio-pill">
          <input type="radio" name="radar-target" id="rt-0" value="0" checked>
          <label for="rt-0">🛸 ISS (NORAD 25544)</label>
        </div>
        <div class="radio-pill">
          <input type="radio" name="radar-target" id="rt-1" value="1">
          <label for="rt-1">🔭 Hubble (NORAD 20580)</label>
        </div>
        <div class="radio-pill">
          <input type="radio" name="radar-target" id="rt-2" value="2">
          <label for="rt-2">🚀 Tiangong (NORAD 48274)</label>
        </div>
      </div>
      <div class="btn-row">
        <button class="btn btn-primary" onclick="saveRadarTarget()">📡 Lock Target</button>
        <button class="btn btn-secondary" onclick="refreshTLE()">↺ Refresh TLE Data</button>
      </div>
    </div>

    <div class="card">
      <div class="card-title">TLE Data Status</div>
      <div class="telem-grid">
        <div class="telem-item">
          <div class="telem-key">ISS TLE</div>
          <div class="telem-val" id="tle-status-0">—</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Hubble TLE</div>
          <div class="telem-val" id="tle-status-1">—</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Tiangong TLE</div>
          <div class="telem-val" id="tle-status-2">—</div>
        </div>
        <div class="telem-item">
          <div class="telem-key">Next Refresh</div>
          <div class="telem-val">Hourly auto</div>
        </div>
      </div>
      <p style="font-size:.68rem;color:var(--c-muted);margin-top:14px">
        TLE data sourced from CelesTrak (celestrak.org). Auto-refreshed every hour.
        Satellite positions are computed using a simplified circular orbit model for visualization purposes.
      </p>
    </div>

  </section>

  <!-- ═══════════════════════════════════════════════
       PANEL: CUSTOM TEXT
  ═══════════════════════════════════════════════════ -->
  <section id="panel-text" class="tab-panel" role="tabpanel" aria-labelledby="tab-text">

    <div class="card">
      <div class="card-title">Custom HUD Message</div>
      <p style="font-size:.78rem;color:var(--c-muted);margin-bottom:16px">
        Enter any message (max 127 characters). Long messages scroll automatically inside the lens viewport.
      </p>
      <label for="custom-text-input" style="font-size:.72rem;color:var(--c-muted);display:block;margin-bottom:6px">MESSAGE TEXT</label>
      <input type="text"
             id="custom-text-input"
             class="text-field"
             maxlength="127"
             placeholder="ORB DISPLAY ONLINE"
             value="ORB DISPLAY ONLINE"
             aria-label="Custom HUD message text">
      <div style="display:flex;justify-content:space-between;align-items:center">
        <span id="char-count" style="font-family:var(--font-mono);font-size:.68rem;color:var(--c-muted)">18 / 127</span>
        <div class="btn-row" style="margin-top:0">
          <button class="btn btn-secondary" onclick="clearText()">✕ Clear</button>
          <button class="btn btn-primary" onclick="sendText()">📤 Send to Display</button>
        </div>
      </div>

      <hr class="divider">
      <p style="font-size:.72rem;color:var(--c-muted);margin-bottom:10px">QUICK PRESETS</p>
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <button class="btn btn-secondary" style="font-size:.72rem;padding:6px 12px" onclick="setPreset('ORB DISPLAY ONLINE')">Online</button>
        <button class="btn btn-secondary" style="font-size:.72rem;padding:6px 12px" onclick="setPreset('ORBITAL SYSTEMS ACTIVE')">Orbital Active</button>
        <button class="btn btn-secondary" style="font-size:.72rem;padding:6px 12px" onclick="setPreset('NO SIGNAL')">No Signal</button>
        <button class="btn btn-secondary" style="font-size:.72rem;padding:6px 12px" onclick="setPreset('TELEMETRY NOMINAL')">Nominal</button>
        <button class="btn btn-secondary" style="font-size:.72rem;padding:6px 12px" onclick="setPreset('SCANNING...')">Scanning</button>
      </div>
    </div>

  </section>

  <!-- ═══════════════════════════════════════════════
       PANEL: TELEMETRY
  ═══════════════════════════════════════════════════ -->
  <section id="panel-telem" class="tab-panel" role="tabpanel" aria-labelledby="tab-telem">

    <div class="card">
      <div class="card-title">Live Device Telemetry</div>
      <div class="telem-grid" id="telem-grid">
        <div class="telem-item"><div class="telem-key">MODE</div><div class="telem-val" id="t-mode">—</div></div>
        <div class="telem-item"><div class="telem-key">UTC TIME</div><div class="telem-val" id="t-time">—</div></div>
        <div class="telem-item"><div class="telem-key">NTP SYNC</div><div class="telem-val" id="t-ntp">—</div></div>
        <div class="telem-item"><div class="telem-key">IP ADDRESS</div><div class="telem-val" id="t-ip">—</div></div>
        <div class="telem-item"><div class="telem-key">FREE HEAP</div><div class="telem-val" id="t-heap">—</div></div>
        <div class="telem-item"><div class="telem-key">CENTER X</div><div class="telem-val" id="t-cx">—</div></div>
        <div class="telem-item"><div class="telem-key">CENTER Y</div><div class="telem-val" id="t-cy">—</div></div>
        <div class="telem-item"><div class="telem-key">RADIUS</div><div class="telem-val" id="t-r">—</div></div>
        <div class="telem-item"><div class="telem-key">SPEED WARP</div><div class="telem-val" id="t-spd">—</div></div>
        <div class="telem-item"><div class="telem-key">RADAR TGT</div><div class="telem-val" id="t-rt">—</div></div>
        <div class="telem-item"><div class="telem-key">TLE ISS</div><div class="telem-val" id="t-tle0">—</div></div>
        <div class="telem-item"><div class="telem-key">TLE HUBBLE</div><div class="telem-val" id="t-tle1">—</div></div>
      </div>
      <div class="btn-row">
        <button class="btn btn-secondary" onclick="loadStatus()">↺ Refresh Telemetry</button>
      </div>
    </div>

  </section>

</div><!-- /wrapper -->

<!-- ═══════════════════════════════════════════════════
     JAVASCRIPT
══════════════════════════════════════════════════════ -->
<script>
'use strict';

/* ─── Utilities ─────────────────────────────────────── */
function setMode(modeValue) {
  fetch('/mode', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mode: modeValue })
  });
}
function toast(msg, type = 'ok') {
  const c = document.getElementById('toast-container');
  const t = document.createElement('div');
  t.className = `toast ${type}`;
  t.textContent = msg;
  c.appendChild(t);
  setTimeout(() => t.remove(), 3500);
}

async function api(path, body) {
  try {
    const opts = body !== undefined
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
      : { method: 'GET' };
    const r = await fetch(path, opts);
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return r;
  } catch (e) {
    toast(`❌ ${path}: ${e.message}`, 'err');
    throw e;
  }
}

/* ─── Status polling ────────────────────────────────── */
const MODE_NAMES = ['SOLAR ORRERY', 'SATELLITE RADAR', 'CUSTOM HUD', 'MAGIC 8 BALL'];
const RADAR_NAMES = ['ISS', 'Hubble', 'Tiangong'];

async function loadStatus() {
  let d;
  try {
    const r = await fetch('/status');
    d = await r.json();
  } catch(e) {
    toast('❌ Cannot reach device', 'err');
    document.getElementById('hdr-ip').textContent = 'Device offline';
    return;
  }
async function shake8Ball() {
  try {
    await fetch('/shake', { method: 'POST' });
    if (typeof toast === 'function') {
      toast('🎱 Magic 8 Ball shaken!', 'ok');
    }
  } catch (err) {
    console.error('Failed to shake:', err);
  }
}

  /* Header chips */
  document.getElementById('hdr-ip').textContent = `http://${d.ip || 'orb.local'}`;
  const chipTime = document.getElementById('chip-time');
  chipTime.textContent = `NTP ${d.timeSynced ? '✓' : '✗'}`;
  chipTime.className = `status-chip ${d.timeSynced ? 'ok' : 'err'}`;
  document.getElementById('chip-wifi').textContent = 'WiFi ✓';
  document.getElementById('chip-wifi').className = 'status-chip ok';
  const heapKB = Math.round(d.freeHeap / 1024);
  document.getElementById('chip-heap').textContent = `${heapKB} KB`;
  document.getElementById('chip-heap').className = `status-chip ${heapKB > 20 ? 'ok' : 'err'}`;
  const tle0ok = d.tleLoaded && d.tleLoaded[0];
  document.getElementById('chip-tle0').textContent = `ISS TLE ${tle0ok ? '✓' : '✗'}`;
  document.getElementById('chip-tle0').className = `status-chip ${tle0ok ? 'ok' : 'err'}`;

  /* Sync UI sliders */
  setSlider('sl-cx', 'val-cx', d.cx ?? 64);
  setSlider('sl-cy', 'val-cy', d.cy ?? 32);
  setSlider('sl-r',  'val-r',  d.radius ?? 28);
  setSlider('sl-speed', 'val-speed', d.speed ?? 100, '×');
  document.getElementById('warp-display').textContent = Math.round(d.speed ?? 100);

  /* Mode cards */
  const m = d.mode ?? 0;
  document.querySelectorAll('[name="mode"]').forEach(r => { r.checked = parseInt(r.value) === m; });
  updateModeCards();

  /* Radar target */
  const rt = d.radarTarget ?? 0;
  document.querySelectorAll('[name="radar-target"]').forEach(r => { r.checked = parseInt(r.value) === rt; });

  /* Custom text */
  const ti = document.getElementById('custom-text-input');
  if (d.customText) ti.value = d.customText;
  updateCharCount();

  /* TLE status */
  ['ISS', 'Hubble', 'Tiangong'].forEach((n, i) => {
    const el = document.getElementById(`tle-status-${i}`);
    if (el) el.textContent = (d.tleLoaded && d.tleLoaded[i]) ? '✓ LOADED' : '✗ PENDING';
    if (el) el.style.color = (d.tleLoaded && d.tleLoaded[i]) ? 'var(--c-success)' : 'var(--c-danger)';
  });

  /* UTC time */
  document.getElementById('utc-time-solar').textContent = d.utcTime || '—';

  /* Telemetry panel */
  document.getElementById('t-mode').textContent  = MODE_NAMES[d.mode] ?? d.mode;
  document.getElementById('t-time').textContent  = d.utcTime;
  document.getElementById('t-ntp').textContent   = d.timeSynced ? '✓ SYNCED' : '✗ OFFLINE';
  document.getElementById('t-ip').textContent    = d.ip;
  document.getElementById('t-heap').textContent  = `${heapKB} KB free`;
  document.getElementById('t-cx').textContent    = d.cx;
  document.getElementById('t-cy').textContent    = d.cy;
  document.getElementById('t-r').textContent     = d.radius + ' px';
  document.getElementById('t-spd').textContent   = (d.speed ?? 100) + '×';
  document.getElementById('t-rt').textContent    = RADAR_NAMES[d.radarTarget] ?? d.radarTarget;
  document.getElementById('t-tle0').textContent  = (d.tleLoaded?.[0]) ? 'LOADED' : 'PENDING';
  document.getElementById('t-tle1').textContent  = (d.tleLoaded?.[1]) ? 'LOADED' : 'PENDING';

  /* Redraw viewport preview */
  drawPreview();
}

/* ─── Slider helpers ────────────────────────────────── */
function setSlider(id, valId, val, suffix = '') {
  const sl = document.getElementById(id);
  const lbl = document.getElementById(valId);
  if (sl) sl.value = val;
  if (lbl) lbl.textContent = val + suffix;
}

function bindSlider(id, valId, suffix = '', onChange) {
  const sl = document.getElementById(id);
  const lbl = document.getElementById(valId);
  if (!sl) return;
  sl.addEventListener('input', () => {
    lbl.textContent = sl.value + suffix;
    if (id === 'sl-speed') {
      document.getElementById('warp-display').textContent = sl.value;
    }
    drawPreview();
    if (onChange) onChange(sl.value);
  });
}

/* ─── Live calibration send ─────────────────────────── */
let calDebounce;
function liveCalibrate() {
  clearTimeout(calDebounce);
  calDebounce = setTimeout(async () => {
    const cx = parseInt(document.getElementById('sl-cx').value);
    const cy = parseInt(document.getElementById('sl-cy').value);
    const r  = parseInt(document.getElementById('sl-r').value);
    try {
      await api('/calibrate', { cx, cy, radius: r });
    } catch(e) {}
  }, 120);
}

bindSlider('sl-cx',    'val-cx', '',  liveCalibrate);
bindSlider('sl-cy',    'val-cy', '',  liveCalibrate);
bindSlider('sl-r',     'val-r',  '',  liveCalibrate);
bindSlider('sl-speed', 'val-speed', '×');

/* ─── Mode selection ────────────────────────────────── */
function updateModeCards() {
  document.querySelectorAll('[name="mode"]').forEach(r => {
    const card = document.getElementById(`mode-card-${r.value}`);
    if (card) card.classList.toggle('active', r.checked);
  });
}

document.querySelectorAll('[name="mode"]').forEach(r => {
  r.addEventListener('change', async () => {
    updateModeCards();
    try {
      await api('/mode', { mode: parseInt(r.value) });
      toast(`✓ Mode → ${MODE_NAMES[r.value]}`, 'ok');
    } catch(e) {}
  });
});

/* ─── Save calibration ──────────────────────────────── */
async function saveCalibration() {
  const cx = parseInt(document.getElementById('sl-cx').value);
  const cy = parseInt(document.getElementById('sl-cy').value);
  const r  = parseInt(document.getElementById('sl-r').value);
  try {
    await api('/calibrate', { cx, cy, radius: r });
    toast('✓ Calibration saved to EEPROM', 'ok');
  } catch(e) {}
}

/* ─── Speed warp ────────────────────────────────────── */
function setSpeed(v) {
  document.getElementById('sl-speed').value = v;
  document.getElementById('val-speed').textContent = v + '×';
  document.getElementById('warp-display').textContent = v;
}

async function saveSpeed() {
  const s = parseFloat(document.getElementById('sl-speed').value);
  try {
    await api('/speed', { speed: s });
    toast(`✓ Speed → ${s}×`, 'ok');
  } catch(e) {}
}

/* ─── Radar target ──────────────────────────────────── */
async function saveRadarTarget() {
  const rt = parseInt(document.querySelector('[name="radar-target"]:checked')?.value ?? 0);
  try {
    await api('/radar', { target: rt });
    toast(`✓ Radar → ${RADAR_NAMES[rt]}`, 'ok');
  } catch(e) {}
}

async function refreshTLE() {
  try {
    await api('/fetchtle');
    toast('↺ TLE refresh queued…', 'ok');
    setTimeout(loadStatus, 8000);
  } catch(e) {}
}

/* ─── Custom text ───────────────────────────────────── */
function updateCharCount() {
  const v = document.getElementById('custom-text-input').value;
  document.getElementById('char-count').textContent = `${v.length} / 127`;
}

document.getElementById('custom-text-input').addEventListener('input', updateCharCount);

async function sendText() {
  const t = document.getElementById('custom-text-input').value.trim();
  if (!t) return;
  try {
    await api('/text', { text: t });
    toast('✓ Text sent to display', 'ok');
  } catch(e) {}
}

function clearText() {
  document.getElementById('custom-text-input').value = '';
  updateCharCount();
}

function setPreset(txt) {
  document.getElementById('custom-text-input').value = txt;
  updateCharCount();
  sendText();
}

/* ─── Tab switching ─────────────────────────────────── */
document.querySelectorAll('.tab-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const panelId = btn.getAttribute('aria-controls');
    document.querySelectorAll('.tab-btn').forEach(b => b.setAttribute('aria-selected', 'false'));
    document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
    btn.setAttribute('aria-selected', 'true');
    document.getElementById(panelId).classList.add('active');
    if (panelId === 'panel-cal') drawPreview();
  });
});

/* ─── OLED Viewport Preview Canvas ─────────────────── */
function drawPreview() {
  const canvas = document.getElementById('oled-preview');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  const SCALE = W / 128;

  // Background
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, W, H);

  const cx = parseInt(document.getElementById('sl-cx').value ?? 64) * SCALE;
  const cy = parseInt(document.getElementById('sl-cy').value ?? 32) * SCALE;
  const r  = parseInt(document.getElementById('sl-r').value  ?? 28) * SCALE;

  // Pixel grid
  ctx.strokeStyle = 'rgba(0,212,255,.04)';
  ctx.lineWidth = 0.5;
  for (let x = 0; x < W; x += SCALE) {
    ctx.beginPath(); ctx.moveTo(x,0); ctx.lineTo(x,H); ctx.stroke();
  }
  for (let y = 0; y < H; y += SCALE) {
    ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
  }

  // Mask outside viewport (dimmed region)
  ctx.save();
  ctx.fillStyle = 'rgba(0,0,0,.7)';
  ctx.fillRect(0, 0, W, H);
  // Clip to viewport circle and clear
  ctx.globalCompositeOperation = 'destination-out';
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();

  // Viewport ring
  ctx.strokeStyle = '#00d4ff';
  ctx.lineWidth = 1.5;
  ctx.shadowBlur = 8;
  ctx.shadowColor = '#00d4ff';
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.stroke();
  ctx.shadowBlur = 0;

  // Center crosshair
  ctx.strokeStyle = 'rgba(0,212,255,.4)';
  ctx.lineWidth = 1;
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy);
  ctx.moveTo(cx, cy - r); ctx.lineTo(cx, cy + r);
  ctx.stroke();
  ctx.setLineDash([]);

  // Center dot
  ctx.fillStyle = '#00d4ff';
  ctx.shadowBlur = 6; ctx.shadowColor = '#00d4ff';
  ctx.beginPath(); ctx.arc(cx, cy, 3, 0, Math.PI * 2); ctx.fill();
  ctx.shadowBlur = 0;

  // Dimension labels
  ctx.fillStyle = 'rgba(0,212,255,.6)';
  ctx.font = `${SCALE * 4}px 'Share Tech Mono', monospace`;
  ctx.textAlign = 'left';
  ctx.fillText(`CX=${Math.round(cx/SCALE)} CY=${Math.round(cy/SCALE)} R=${Math.round(r/SCALE)}`, 6, 14);

  // Panel boundary
  ctx.strokeStyle = 'rgba(0,212,255,.15)';
  ctx.lineWidth = 1;
  ctx.strokeRect(0.5, 0.5, W-1, H-1);
}

/* ─── Auto-poll every 5 s ───────────────────────────── */
loadStatus();
setInterval(loadStatus, 5000);
</script>

</body>
</html>
)rawhtml";
