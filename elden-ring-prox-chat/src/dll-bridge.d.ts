export {};

declare global {
  interface Window {
    dllBridge: {
      onReading: (callback: (line: string) => void) => () => void;
      onStatus: (callback: (message: string) => void) => () => void;
      onMmEvent: (callback: (line: string) => void) => () => void;
      onConnectionState: (callback: (connected: boolean) => void) => () => void;
      getConnectionState: () => Promise<boolean>;
      sendCommand: (commandName: string, params?: Record<string, string>) => Promise<boolean>;
    };
  }
}
