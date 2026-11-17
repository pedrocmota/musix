import { useCallback, useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { getVersion } from "@tauri-apps/api/app";
import { getCurrentWindow, LogicalSize } from "@tauri-apps/api/window";
import type { ActionResult, Config, Output, Status } from "./types";
import { LANGS, useI18n, type Lang } from "./i18n";

const DEFAULT_CONFIG: Config = { enabled: true, voiceCancel: false, pitch: 0, order: 0 };

function Logo() {
  return (
    <svg className="logo" viewBox="0 0 1024 1024" aria-hidden="true">
      <defs>
        <linearGradient id="lbg" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#1b1535" />
          <stop offset="1" stopColor="#0d0b1a" />
        </linearGradient>
        <linearGradient id="lm" gradientUnits="userSpaceOnUse" x1="200" y1="760" x2="824" y2="300">
          <stop offset="0" stopColor="#7c3aed" />
          <stop offset="0.55" stopColor="#c026d3" />
          <stop offset="1" stopColor="#fb7185" />
        </linearGradient>
      </defs>
      <rect x="64" y="64" width="896" height="896" rx="220" fill="url(#lbg)" />
      <path d="M120 512C180 512 180 430 240 430S300 594 360 594 420 430 480 430 540 594 600 594 660 430 720 430 780 594 840 594 880 512 904 512" fill="none" stroke="#fff" strokeOpacity="0.28" strokeWidth="30" strokeLinecap="round" />
      <path d="M232 744L232 318 512 598 792 318 792 744" fill="none" stroke="url(#lm)" strokeWidth="118" strokeLinecap="round" strokeLinejoin="round" />
      <circle cx="512" cy="598" r="92" fill="url(#lbg)" />
      <circle cx="512" cy="598" r="52" fill="#fff" />
    </svg>
  );
}

function GearIcon() {
  return (
    <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
      <circle cx="12" cy="12" r="3" />
      <path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3h.1a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8v.1a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z" />
    </svg>
  );
}

function BackIcon() {
  return (
    <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M15 18l-6-6 6-6" />
    </svg>
  );
}

function Chevron({ up }: { up?: boolean }) {
  return (
    <svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
      <path d={up ? "M6 15l6-6 6 6" : "M6 9l6 6 6-6"} />
    </svg>
  );
}

function PitchWave({ semitone }: { semitone: number }) {
  const W = 200, H = 56, MID = W / 2, Y = H / 2, A = 17, PAD = 8, CYCLES = 2.5;
  const ratio = Math.pow(2, semitone / 12);
  const pts: string[] = [];
  for (let x = PAD; x <= W - PAD; x += 2) {
    const left = x < MID;
    const u = left ? (x - PAD) / (MID - PAD) : (x - MID) / (W - PAD - MID);
    const phase = left ? CYCLES * u : CYCLES * (1 + u * ratio);
    const amp = left ? A : A * (1 - 0.45 * u);
    const y = Y - amp * Math.sin(2 * Math.PI * phase);
    pts.push(`${pts.length ? "L" : "M"}${x} ${y.toFixed(1)}`);
  }
  return (
    <svg className="wave" viewBox={`0 0 ${W} ${H}`} aria-hidden="true">
      <line className="wave-div" x1={MID} y1={6} x2={MID} y2={H - 6} strokeDasharray="3 3" />
      <path className="wave-sig" d={pts.join(" ")} fill="none" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

function Switch({ checked, onChange, label, hint, disabled }: { checked: boolean; onChange: (v: boolean) => void; label: string; hint?: string; disabled?: boolean }) {
  return (
    <label className={"switch" + (disabled ? " disabled" : "")}>
      <input type="checkbox" checked={checked} disabled={disabled} onChange={(e) => onChange(e.target.checked)} />
      <span className="track"><span className="thumb" /></span>
      <span className="switch-text">
        <span className="switch-label">{label}</span>
        {hint && <span className="muted small">{hint}</span>}
      </span>
    </label>
  );
}

type View = "main" | "settings";

export default function App() {
  const { lang, setLang, t, terr } = useI18n();
  const [view, setView] = useState<View>("main");
  const [config, setConfig] = useState<Config>(DEFAULT_CONFIG);
  const [status, setStatus] = useState<Status | null>(null);
  const [outputs, setOutputs] = useState<Output[]>([]);
  const [busy, setBusy] = useState<string | null>(null);
  const [log, setLog] = useState<string>("");
  const [error, setError] = useState<unknown>(null);
  const [version, setVersion] = useState<string>("");
  const saveTimer = useRef<number | null>(null);
  const appRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    getVersion().then(setVersion).catch(() => {});
  }, []);
  const viewRef = useRef<View>("main");
  viewRef.current = view;

  useEffect(() => {
    const el = appRef.current;
    if (!el) return;
    const MAX_H = 900;
    let timer: number | null = null;
    const fit = () => {
      timer = null;
      if (viewRef.current !== "main") return;
      const h = Math.min(Math.ceil(el.getBoundingClientRect().height), MAX_H);
      const win = getCurrentWindow();
      Promise.all([win.innerSize(), win.scaleFactor()])
        .then(([size, sf]) => {
          const logical = size.toLogical(sf);
          if (Math.abs(logical.height - h) > 2) return win.setSize(new LogicalSize(logical.width, h));
        })
        .catch(() => {});
    };
    const ro = new ResizeObserver(() => {
      if (timer) window.clearTimeout(timer);
      timer = window.setTimeout(fit, 60);
    });
    ro.observe(el);
    fit();
    return () => {
      ro.disconnect();
      if (timer) window.clearTimeout(timer);
    };
  }, []);

  const refreshOutputs = useCallback(async () => {
    try {
      setOutputs(await invoke<Output[]>("list_outputs"));
    } catch (e) {
      setError(e);
    }
  }, []);

  const refreshStatus = useCallback(async () => {
    try {
      setStatus(await invoke<Status>("get_status"));
    } catch {}
  }, []);

  useEffect(() => {
    invoke<Config>("get_config").then(setConfig).catch(() => {});
    invoke<string[]>("launch_args")
      .then((a) => {
        if (a.includes("--settings")) setView("settings");
        const i = a.indexOf("--lang");
        if (i >= 0 && (a[i + 1] === "pt" || a[i + 1] === "en")) setLang(a[i + 1] as Lang);
      })
      .catch(() => {});
    refreshOutputs();
    refreshStatus();
    const timer = window.setInterval(refreshStatus, 1000);
    return () => window.clearInterval(timer);
  }, [refreshOutputs, refreshStatus, setLang]);

  useEffect(() => {
    document.documentElement.lang = lang === "pt" ? "pt-BR" : "en";
  }, [lang]);

  const updateConfig = (patch: Partial<Config>) => {
    const next = { ...config, ...patch };
    setConfig(next);
    if (saveTimer.current) window.clearTimeout(saveTimer.current);
    saveTimer.current = window.setTimeout(() => {
      invoke("set_config", { config: next }).then(() => setError(null)).catch((e) => setError(e));
    }, 120);
  };

  const runAction = async (cmd: "install_output" | "uninstall_output", out: Output) => {
    setBusy(out.guid);
    setLog("");
    setError(null);
    try {
      const r = await invoke<ActionResult>(cmd, { guid: out.guid, lang });
      setLog(r.log);
      if (!r.ok) setError(t("actionFailed", { code: r.code }));
    } catch (e) {
      setError(e);
    } finally {
      setBusy(null);
      refreshOutputs();
      refreshStatus();
    }
  };

  const setPitch = (p: number) => updateConfig({ pitch: Math.max(-4, Math.min(4, p)) });
  const onPitchKey = (e: React.KeyboardEvent) => {
    if (!config.enabled) return;
    const step = { ArrowUp: 1, ArrowDown: -1, PageUp: 4, PageDown: -4 }[e.key];
    const abs = { Home: -4, End: 4 }[e.key];
    if (step === undefined && abs === undefined) return;
    e.preventDefault();
    setPitch(abs !== undefined ? abs : config.pitch + (step ?? 0));
  };

  const installed = outputs.filter((o) => o.installed);
  const defaultOut = outputs.find((o) => o.isDefault);
  const defaultNotInstalled = defaultOut !== undefined && !defaultOut.installed;

  const statusText = !status
    ? "…"
    : installed.length === 0
      ? t("statusNone")
      : !status.present
        ? t("statusNotLoaded")
        : status.locked
          ? t("statusProcessing", { rate: status.rate, channels: status.channels })
          : t("statusIdle", { time: status.time });

  const errorText = error === null ? "" : typeof error === "string" ? error : terr(error);

  return (
    <div className="app" ref={appRef}>
      <header>
        <Logo />
        <div className="head-text">
          <h1>Musix</h1>
          <p className="muted">{t("subtitle")}</p>
        </div>
        {view === "main" ? (
          <button type="button" className="icon-btn" title={t("settings")} aria-label={t("settings")} onClick={() => { setView("settings"); refreshOutputs(); }}>
            <GearIcon />
          </button>
        ) : (
          <button type="button" className="icon-btn" title={t("back")} aria-label={t("back")} onClick={() => setView("main")}>
            <BackIcon />
          </button>
        )}
      </header>

      {view === "main" && (
        <>
          <section className="card">
            <h2>{t("effect")}</h2>
            <div className="controls">
              <Switch label={t("enabled")} checked={config.enabled} onChange={(v) => updateConfig({ enabled: v })} />
              <Switch label={t("voiceCancel")} hint={t("voiceCancelHint")} checked={config.voiceCancel} disabled={!config.enabled} onChange={(v) => updateConfig({ voiceCancel: v })} />
              <div className={"pitch" + (config.enabled ? "" : " disabled")}>
                <span className="switch-text">
                  <span className="switch-label">{t("pitch")}</span>
                  <span className="muted small">{t("pitchHint")}</span>
                </span>
                <div className="pitch-ctl">
                  <PitchWave semitone={config.pitch} />
                  <div
                    className="spin"
                    role="spinbutton"
                    tabIndex={config.enabled ? 0 : -1}
                    aria-label={t("pitch")}
                    aria-valuemin={-4}
                    aria-valuemax={4}
                    aria-valuenow={config.pitch}
                    aria-valuetext={t("pitchValue", { n: config.pitch })}
                    aria-disabled={!config.enabled}
                    onKeyDown={onPitchKey}
                    onWheel={(e) => { if (config.enabled && e.deltaY !== 0) setPitch(config.pitch + (e.deltaY < 0 ? 1 : -1)); }}
                  >
                    <span className="spin-text">{t("pitchValue", { n: config.pitch })}</span>
                    <span className="spin-arrows">
                      <button type="button" className="spin-btn" tabIndex={-1} aria-label={t("pitchUp")} disabled={!config.enabled || config.pitch >= 4} onClick={() => setPitch(config.pitch + 1)}><Chevron up /></button>
                      <button type="button" className="spin-btn" tabIndex={-1} aria-label={t("pitchDown")} disabled={!config.enabled || config.pitch <= -4} onClick={() => setPitch(config.pitch - 1)}><Chevron /></button>
                    </span>
                  </div>
                </div>
              </div>
              <Switch label={t("order")} hint={t("orderHint")} checked={config.order === 1} disabled={!config.enabled} onChange={(v) => updateConfig({ order: v ? 1 : 0 })} />
            </div>
          </section>

          <section className="card slim">
            <p className={"status" + (status?.locked ? " ok" : "")}>{statusText}</p>
            <p className="muted small">
              {installed.length === 0 ? "" : installed.length === 1 ? t("installedIn", { name: installed[0].name }) : t("installedCount", { n: installed.length })}
              {defaultNotInstalled && installed.length > 0 && defaultOut && t("defaultNotInstalled", { name: defaultOut.name })}
            </p>
            {errorText && <p className="error">{errorText}</p>}
          </section>
        </>
      )}

      {view === "settings" && (
        <>
          <section className="card slim">
            <div className="lang-row">
              <span className="switch-label">{t("language")}</span>
              <div className="seg">
                {LANGS.map((l) => (
                  <button key={l.id} type="button" className={"seg-btn" + (lang === l.id ? " active" : "")} onClick={() => setLang(l.id)}>
                    {l.label}
                  </button>
                ))}
              </div>
            </div>
            <div className="lang-row version-row">
              <span className="switch-label">{t("version")}</span>
              <span className="muted">{version ? `v${version}` : "…"}</span>
            </div>
          </section>

          <section className="card">
            <div className="card-head">
              <h2>{t("outputs")}</h2>
              <button type="button" className="ghost" onClick={refreshOutputs} disabled={busy !== null}>{t("refresh")}</button>
            </div>
            <ul className="outputs">
              {outputs.map((o) => (
                <li key={o.id} className={o.installed ? "installed" : ""}>
                  <div className="out-info">
                    <div className="out-name">
                      {o.name}
                      {o.isDefault && <span className="badge default">{t("default")}</span>}
                      {o.installed && <span className="badge on">{t("installedBadge")}</span>}
                    </div>
                    <div className="muted small">{o.iface}</div>
                  </div>
                  {o.installed ? (
                    <button type="button" className="danger" disabled={busy !== null} onClick={() => runAction("uninstall_output", o)}>
                      {busy === o.guid ? t("removing") : t("uninstall")}
                    </button>
                  ) : (
                    <button type="button" disabled={busy !== null} onClick={() => runAction("install_output", o)}>
                      {busy === o.guid ? t("installing") : t("install")}
                    </button>
                  )}
                </li>
              ))}
              {outputs.length === 0 && <li className="muted">{t("noOutputs")}</li>}
            </ul>
            <p className="muted small">{t("outputsHelp")}</p>
            {errorText && <p className="error">{errorText}</p>}
            {log && (
              <details open>
                <summary>{t("lastActionLog")}</summary>
                <pre>{log}</pre>
              </details>
            )}
          </section>
        </>
      )}
    </div>
  );
}
