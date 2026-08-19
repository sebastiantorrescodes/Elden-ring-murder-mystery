export {};

declare global {
  interface Window {
    dllBridge: {
      onReading: (callback: (line: string) => void) => () => void;
    };
  }
}
