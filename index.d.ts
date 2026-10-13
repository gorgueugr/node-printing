/**
 * Type definitions for node-printing (package "printing").
 */

/** Document data type accepted by printDirect/printFile. */
export type PrintFormat =
  | 'RAW'
  | 'TEXT'
  | 'PDF'
  | 'POSTSCRIPT'
  | 'JPEG'
  | 'PNG'
  | 'COMMAND'
  | 'AUTO'
  | (string & {}); // a raw MIME type such as "application/pdf"

/** Job command accepted by setJob (availability depends on the platform). */
export type JobCommand =
  | 'CANCEL'
  | 'DELETE'
  | 'PAUSE'
  | 'RESUME'
  | 'RESTART'
  | 'HOLD'
  | 'RELEASE'
  | 'RETAIN'
  | 'SENT-TO-PRINTER'
  | 'LAST-PAGE-EJECTED'
  | (string & {});

export type PrinterState = 'idle' | 'processing' | 'stopped' | 'unknown';

export type JobState =
  | 'pending'
  | 'processing'
  | 'held'
  | 'stopped'
  | 'canceled'
  | 'aborted'
  | 'completed'
  | 'unknown';

export interface PrinterInfo {
  name: string;
  /** CUPS instance name, null when the queue has no instance. */
  instance: string | null;
  /** Print server, empty for local queues. */
  server: string;
  shareName: string;
  portName: string;
  /** Driver name (Windows) or "make and model" (CUPS). */
  driverName: string;
  location: string;
  /** Queue description (CUPS printer-info). */
  comment: string;
  /** Device URI on CUPS, port based identifier on Windows. */
  uri: string;
  state: PrinterState;
  isDefault: boolean;
  isShared: boolean;
  isLocal: boolean;
  acceptsJobs: boolean;
  /** Raw platform status code (PRINTER_STATUS_* on Windows, IPP on CUPS). */
  status: number;
  /** Canonical status names, e.g. ["idle"], ["PRINTING"], ["PAPER-OUT"]. */
  statusArray: string[];
  /** Raw attributes bitmask (Windows printer-type / PRINTER_ATTRIBUTE_*). */
  attributes: number;
  /** Canonical attribute names, e.g. ["COLOR", "DUPLEX"]. */
  attributeArray: string[];
  /** Pages per minute, 0 when unknown. */
  averagePPM: number;
  /** Jobs currently queued for this printer. */
  cJobs: number;
  defaultPriority: number;
  startTime: number;
  untilTime: number;
  /** Raw platform options: CUPS destination options or Windows printer data. */
  options: Record<string, string>;
}

export interface JobInfo {
  id: number;
  /** Document title. */
  name: string;
  printerName: string;
  user: string;
  /** Short format name (RAW, PDF...) or the raw MIME type. */
  format: string;
  state: JobState;
  /** Human readable summary: PRINTING, PAUSED, PRINTED... */
  status: string;
  /** Canonical status reasons, e.g. ["PRINTING", "PENDING"]. */
  statusArray: string[];
  priority: number;
  /** Document size in bytes. */
  size: number;
  /** 1 based position in the queue, 0 when unknown. */
  position: number;
  totalPages: number;
  pagesPrinted: number;
  submittedTime: Date | null;
  completedTime: Date | null;
  processingTime: Date | null;
}

export interface PrinterDevMode {
  deviceName: string;
  /** "A4", "LETTER", "A3"... empty when unknown. */
  paperSize: string;
  /** Raw platform paper code (DMPAPER_* on Windows, 0 on CUPS). */
  paperSizeCode: number;
  mediaType: string;
  orientation: 'PORTRAIT' | 'LANDSCAPE';
  duplex: 'SIMPLEX' | 'VERTICAL' | 'HORIZONTAL';
  color: 'MONOCHROME' | 'COLOR';
  printQuality: 'DRAFT' | 'LOW' | 'MEDIUM' | 'HIGH';
  copies: number;
  collate: boolean;
  scale: number;
  /** "Auto", "Tray 1", "Manual"... empty when unknown. */
  defaultSource: string;
  defaultSourceCode: number;
  /** Dots per inch, 0 when unknown. */
  resolutionX: number;
  resolutionY: number;
}

/** Driver options: { optionKeyword: { choice: isSelected } }. */
export type PrinterDriverOptions = Record<string, Record<string, boolean>>;

/** Every print queue of the system. */
export function getPrinters(): PrinterInfo[];

/** Name of the default queue, empty string when none is configured. */
export function getDefaultPrinterName(): string;

/** The default queue as an object, or null when there is no default queue. */
export function getDefaultPrinter(): PrinterInfo | null;

/** One queue by name ("queue/instance" also accepted on CUPS). */
export function getPrinter(printerName: string): PrinterInfo;

/** Default configuration of a queue. */
export function getPrinterDevMode(printerName: string): PrinterDevMode;

/**
 * Driver options of a queue. Empty object for queues without driver options
 * (for example CUPS raw queues).
 */
export function getPrinterDriverOptions(printerName: string): PrinterDriverOptions;

/** Jobs of one queue, or of every queue when printerName is omitted. */
export function getJobs(printerName?: string, includeCompleted?: boolean): JobInfo[];

/** One job by id. */
export function getJob(printerName: string, jobId: number): JobInfo;

/** Controls a job. Returns true when the spooler accepted the command. */
export function setJob(printerName: string, jobId: number, command: JobCommand): boolean;

/** Cancels a job (shorthand for setJob(printerName, jobId, 'CANCEL')). */
export function cancelJob(printerName: string, jobId: number): boolean;

/** Commands accepted by setJob on this platform. */
export function getSupportedJobCommands(): string[];

/** Data types accepted by the print functions on this platform. */
export function getSupportedPrintFormats(): string[];

/** Sends a string or a Buffer to a queue and waits for the spooler. */
export function printDirect(
  data: string | Buffer | Uint8Array,
  printerName: string,
  docName?: string,
  type?: PrintFormat
): number;

/** Same as printDirect but asynchronous: resolves with the spooler job id. */
export function printDirectAsync(
  data: string | Buffer | Uint8Array,
  printerName: string,
  docName?: string,
  type?: PrintFormat
): Promise<number>;

/** Sends a file to a queue. */
export function printFile(
  filename: string,
  docName: string,
  printerName: string,
  type: PrintFormat
): number;

/** Same as printFile but asynchronous: resolves with the spooler job id. */
export function printFileAsync(
  filename: string,
  docName: string,
  printerName: string,
  type: PrintFormat
): Promise<number>;

declare const printing: {
  getPrinters: typeof getPrinters;
  getDefaultPrinterName: typeof getDefaultPrinterName;
  getDefaultPrinter: typeof getDefaultPrinter;
  getPrinter: typeof getPrinter;
  getPrinterDevMode: typeof getPrinterDevMode;
  getPrinterDriverOptions: typeof getPrinterDriverOptions;
  getJobs: typeof getJobs;
  getJob: typeof getJob;
  setJob: typeof setJob;
  cancelJob: typeof cancelJob;
  getSupportedJobCommands: typeof getSupportedJobCommands;
  getSupportedPrintFormats: typeof getSupportedPrintFormats;
  printDirect: typeof printDirect;
  printDirectAsync: typeof printDirectAsync;
  printFile: typeof printFile;
  printFileAsync: typeof printFileAsync;
};

export default printing;
