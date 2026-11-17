export interface Output {
  id: string;
  guid: string;
  name: string;
  iface: string;
  isDefault: boolean;
  installed: boolean;
  hasBackup: boolean;
}

export interface Config {
  enabled: boolean;
  voiceCancel: boolean;
  pitch: number;
  order: number;
}

export interface Status {
  present: boolean;
  locked: boolean;
  rate: number;
  channels: number;
  pid: number;
  time: string;
  configWritable: boolean;
}

export interface ActionResult {
  ok: boolean;
  code: number;
  log: string;
}
