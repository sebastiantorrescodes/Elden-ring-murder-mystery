// See the Electron documentation for details on how to use preload scripts:
// https://www.electronjs.org/docs/latest/tutorial/process-model#preload-scripts

import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('dllBridge', {
  onReading: (callback: (line: string) => void) => {
    const listener = (_event: Electron.IpcRendererEvent, line: string) => callback(line);
    ipcRenderer.on('dll-reading', listener);
    return () => ipcRenderer.removeListener('dll-reading', listener);
  },
  onStatus: (callback: (message: string) => void) => {
    const listener = (_event: Electron.IpcRendererEvent, message: string) => callback(message);
    ipcRenderer.on('dll-status', listener);
    return () => ipcRenderer.removeListener('dll-status', listener);
  },
  onMmEvent: (callback: (line: string) => void) => {
  const listener = (
    _event: Electron.IpcRendererEvent,
    line: string,
  ) => callback(line);

  ipcRenderer.on('mm-event', listener);

  return () =>
    ipcRenderer.removeListener(
      'mm-event',
      listener,
    );
},
  onConnectionState: (callback: (connected: boolean) => void) => {
    const listener = (
      _event: Electron.IpcRendererEvent,
      connected: boolean,
    ) => callback(connected);
    ipcRenderer.on('mm-connection-state', listener);
    return () => ipcRenderer.removeListener('mm-connection-state', listener);
  },
  getConnectionState: (): Promise<boolean> =>
    ipcRenderer.invoke('mm-get-connection-state'),
  sendCommand: (commandName: string, params?: Record<string, string>): Promise<boolean> =>
    ipcRenderer.invoke('mm-send-command', commandName, params),
});
