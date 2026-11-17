import { useCallback, useState } from "react";

export type Lang = "pt" | "en";

const STORAGE_KEY = "musix.lang";

const dict = {
  pt: {
    subtitle: "Karaoke do Realtek Audio Console em qualquer placa de som",
    settings: "Configurações",
    back: "Voltar",
    effect: "Efeito",
    enabled: "Ligado",
    voiceCancel: "Cancelar voz",
    voiceCancelHint: "Remove o que está no centro do estéreo, mantendo o grave (VoiceCancel)",
    pitch: "Tom",
    pitchHint: "Semitons, de -4 a +4",
    pitchValue: "Karaoke {n}",
    pitchUp: "Subir um semitom",
    pitchDown: "Descer um semitom",
    order: "Tom antes do cancelamento",
    orderHint: "Ordem invertida dos dois efeitos",
    statusNone: "O Musix ainda não está em nenhuma saída. Abra as configurações para instalar.",
    statusNotLoaded: "APO ainda não carregou: toque um som em uma saída instalada.",
    statusProcessing: "Processando · {rate} Hz · {channels} canais",
    statusIdle: "Pronto, sem som tocando no momento (último stream: {time}).",
    installedIn: "Instalado em: {name}.",
    installedCount: "Instalado em {n} saídas.",
    defaultNotInstalled: " A saída padrão ({name}) não tem o Musix.",
    outputs: "Saídas de áudio",
    refresh: "Atualizar",
    default: "Padrão",
    installedBadge: "Musix instalado",
    install: "Instalar",
    uninstall: "Desinstalar",
    installing: "Instalando…",
    removing: "Removendo…",
    noOutputs: "Nenhuma saída ativa encontrada.",
    outputsHelp:
      "O efeito só vale para as saídas onde o Musix está instalado. Instalar e desinstalar pedem permissão de administrador e reiniciam o áudio do Windows por um instante. Em saídas com APO de fabricante (Realtek etc.) o Musix entra junto, sem removê-lo; em saídas com o APO genérico do Windows ele ocupa o slot, e a desinstalação devolve o original.",
    actionFailed: "A ação terminou com código {code}. Veja o registro abaixo.",
    lastActionLog: "Registro da última ação",
    language: "Idioma",
    version: "Versão",
    "err.config_dir": "A pasta de configuração ainda não existe: instale o Musix em uma saída primeiro.",
    "err.config_write": "Não foi possível gravar a configuração",
    "err.uac_cancelled": "A permissão de administrador foi negada (UAC cancelado).",
    "err.elevate": "Não foi possível executar como administrador",
    "err.elevated_process": "O processo elevado não foi criado.",
    "err.invalid_guid": "Identificador de saída inválido.",
    "err.resource": "Recurso MusixAPO.dll não encontrado",
    "err.exe": "Caminho do executável desconhecido",
    "err.enumerate": "Falha ao enumerar as saídas de áudio",
  },
  en: {
    subtitle: "Realtek Audio Console's Karaoke on any sound card",
    settings: "Settings",
    back: "Back",
    effect: "Effect",
    enabled: "Enabled",
    voiceCancel: "Voice cancel",
    voiceCancelHint: "Removes what sits in the center of the stereo image, keeping the bass (VoiceCancel)",
    pitch: "Pitch",
    pitchHint: "Semitones, from -4 to +4",
    pitchValue: "Karaoke {n}",
    pitchUp: "Up one semitone",
    pitchDown: "Down one semitone",
    order: "Pitch before voice cancel",
    orderHint: "Reversed order of the two effects",
    statusNone: "Musix is not installed on any output yet. Open the settings to install it.",
    statusNotLoaded: "The APO has not loaded yet: play some audio on an installed output.",
    statusProcessing: "Processing · {rate} Hz · {channels} channels",
    statusIdle: "Ready, nothing playing right now (last stream: {time}).",
    installedIn: "Installed on: {name}.",
    installedCount: "Installed on {n} outputs.",
    defaultNotInstalled: " The default output ({name}) does not have Musix.",
    outputs: "Audio outputs",
    refresh: "Refresh",
    default: "Default",
    installedBadge: "Musix installed",
    install: "Install",
    uninstall: "Uninstall",
    installing: "Installing…",
    removing: "Removing…",
    noOutputs: "No active output found.",
    outputsHelp:
      "The effect only applies to outputs where Musix is installed. Installing and uninstalling ask for administrator permission and restart Windows audio for a moment. On outputs with a vendor APO (Realtek etc.) Musix is chained alongside it; on outputs with the generic Windows APO it takes the slot, and uninstalling restores the original.",
    actionFailed: "The action finished with code {code}. See the log below.",
    lastActionLog: "Log of the last action",
    language: "Language",
    version: "Version",
    "err.config_dir": "The configuration folder does not exist yet: install Musix on an output first.",
    "err.config_write": "Could not write the configuration",
    "err.uac_cancelled": "Administrator permission was denied (UAC cancelled).",
    "err.elevate": "Could not run as administrator",
    "err.elevated_process": "The elevated process was not created.",
    "err.invalid_guid": "Invalid output identifier.",
    "err.resource": "MusixAPO.dll resource not found",
    "err.exe": "Unknown executable path",
    "err.enumerate": "Failed to enumerate audio outputs",
  },
} as const;

export type Key = keyof typeof dict.pt;

export const LANGS: { id: Lang; label: string }[] = [
  { id: "pt", label: "Português" },
  { id: "en", label: "English" },
];

function detect(): Lang {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved === "pt" || saved === "en") return saved;
  } catch {}
  return navigator.language.toLowerCase().startsWith("pt") ? "pt" : "en";
}

export function useI18n() {
  const [lang, setLangState] = useState<Lang>(detect);
  const setLang = useCallback((l: Lang) => {
    setLangState(l);
    try {
      localStorage.setItem(STORAGE_KEY, l);
    } catch {}
  }, []);
  const t = useCallback(
    (key: Key, params?: Record<string, string | number>) => {
      let s: string = dict[lang][key] ?? dict.en[key] ?? key;
      if (params) for (const [k, v] of Object.entries(params)) s = s.replace(`{${k}}`, String(v));
      return s;
    },
    [lang],
  );
  const terr = useCallback(
    (e: unknown) => {
      const raw = String(e);
      if (!raw.startsWith("err.")) return raw;
      const [key, ...rest] = raw.split("|");
      const detail = rest.join("|");
      const base = (dict[lang] as Record<string, string>)[key] ?? key;
      return detail ? `${base}: ${detail}` : base;
    },
    [lang],
  );
  return { lang, setLang, t, terr };
}
