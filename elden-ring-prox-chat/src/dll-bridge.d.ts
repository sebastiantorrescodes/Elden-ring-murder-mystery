export {};

declare global {
  interface Window {
    dllBridge: {
      onReading: (callback: (line: string) => void) => () => void;
      onStatus: (callback: (message: string) => void) => () => void;
      sendCommand: (commandName: string, params?: Record<string, string>) => Promise<boolean>;
    };
  }
}
