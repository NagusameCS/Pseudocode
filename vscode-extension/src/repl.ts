/*
 * Pseudocode REPL
 * Interactive Read-Eval-Print Loop for Pseudocode
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import { spawn, ChildProcess } from 'child_process';

let replTerminal: vscode.Terminal | undefined;
let replProcess: ChildProcess | undefined;

export async function startRepl(vmPath: string | null): Promise<void> {
    if (!vmPath) {
        vscode.window.showErrorMessage('Pseudocode VM not found. Please set pseudocode.vmPath in settings.');
        return;
    }

    // Check if REPL is already running
    if (replTerminal) {
        replTerminal.show();
        return;
    }

    // Create a pseudo-terminal for the REPL
    const writeEmitter = new vscode.EventEmitter<string>();
    const closeEmitter = new vscode.EventEmitter<number>();

    let line = '';
    let history: string[] = [];
    let historyIndex = -1;
    let multilineBuffer = '';
    let inMultiline = false;

    const pty: vscode.Pseudoterminal = {
        onDidWrite: writeEmitter.event,
        onDidClose: closeEmitter.event,

        open: () => {
            writeEmitter.fire('\x1b[1;36m╔══════════════════════════════════════════════════════╗\x1b[0m\r\n');
            writeEmitter.fire('\x1b[1;36m║\x1b[0m  \x1b[1;33mPseudocode Interactive REPL\x1b[0m                          \x1b[1;36m║\x1b[0m\r\n');
            writeEmitter.fire('\x1b[1;36m║\x1b[0m  Type expressions to evaluate. Use Ctrl+D to exit.    \x1b[1;36m║\x1b[0m\r\n');
            writeEmitter.fire('\x1b[1;36m╚══════════════════════════════════════════════════════╝\x1b[0m\r\n');
            writeEmitter.fire('\r\n');
            writePrompt();
        },

        close: () => {
            if (replProcess) {
                replProcess.kill();
                replProcess = undefined;
            }
            replTerminal = undefined;
        },

        handleInput: (data: string) => {
            // Handle special characters
            if (data === '\x03') {
                // Ctrl+C - cancel current line
                writeEmitter.fire('^C\r\n');
                line = '';
                multilineBuffer = '';
                inMultiline = false;
                writePrompt();
                return;
            }

            if (data === '\x04') {
                // Ctrl+D - exit
                writeEmitter.fire('\r\n\x1b[33mGoodbye!\x1b[0m\r\n');
                closeEmitter.fire(0);
                return;
            }

            if (data === '\x7f') {
                // Backspace
                if (line.length > 0) {
                    line = line.slice(0, -1);
                    writeEmitter.fire('\b \b');
                }
                return;
            }

            if (data === '\x1b[A') {
                // Up arrow - history
                if (history.length > 0 && historyIndex < history.length - 1) {
                    historyIndex++;
                    // Clear current line
                    while (line.length > 0) {
                        writeEmitter.fire('\b \b');
                        line = line.slice(0, -1);
                    }
                    line = history[history.length - 1 - historyIndex];
                    writeEmitter.fire(line);
                }
                return;
            }

            if (data === '\x1b[B') {
                // Down arrow - history
                if (historyIndex > 0) {
                    historyIndex--;
                    while (line.length > 0) {
                        writeEmitter.fire('\b \b');
                        line = line.slice(0, -1);
                    }
                    line = history[history.length - 1 - historyIndex];
                    writeEmitter.fire(line);
                } else if (historyIndex === 0) {
                    historyIndex = -1;
                    while (line.length > 0) {
                        writeEmitter.fire('\b \b');
                        line = line.slice(0, -1);
                    }
                }
                return;
            }

            if (data === '\r') {
                // Enter
                writeEmitter.fire('\r\n');

                if (line.trim() === '') {
                    if (inMultiline) {
                        // Empty line in multiline mode - execute buffer
                        executeCode(multilineBuffer, writeEmitter, () => {
                            history.push(multilineBuffer);
                            multilineBuffer = '';
                            inMultiline = false;
                            writePrompt();
                        });
                    } else {
                        writePrompt();
                    }
                    line = '';
                    return;
                }

                // Check for multiline constructs
                const trimmed = line.trim();
                const opensBlock = trimmed.match(/^(fn\s+|if\s+|while\s+|for\s+|match\s+)/);
                const closesBlock = trimmed === 'end';

                if (inMultiline) {
                    multilineBuffer += line + '\n';
                    if (closesBlock) {
                        // Check if we've closed all blocks
                        const opens = (multilineBuffer.match(/\b(fn|if|while|for|match)\b/g) || []).length;
                        const closes = (multilineBuffer.match(/\bend\b/g) || []).length;
                        if (closes >= opens) {
                            executeCode(multilineBuffer, writeEmitter, () => {
                                history.push(multilineBuffer.trim());
                                multilineBuffer = '';
                                inMultiline = false;
                                writePrompt();
                            });
                        } else {
                            writeContinuation();
                        }
                    } else {
                        writeContinuation();
                    }
                } else if (opensBlock) {
                    multilineBuffer = line + '\n';
                    inMultiline = true;
                    writeContinuation();
                } else {
                    // Single line execution
                    executeCode(line, writeEmitter, () => {
                        history.push(line);
                        writePrompt();
                    });
                }

                line = '';
                historyIndex = -1;
                return;
            }

            // Regular character
            if (data >= ' ' && data <= '~') {
                line += data;
                writeEmitter.fire(data);
            }
        }
    };

    function writePrompt() {
        writeEmitter.fire('\x1b[1;32mpseudo>\x1b[0m ');
    }

    function writeContinuation() {
        writeEmitter.fire('\x1b[1;33m  ...>\x1b[0m ');
    }

    async function executeCode(
        code: string,
        writer: vscode.EventEmitter<string>,
        onComplete: () => void
    ) {
        // Create a temp file with the code
        const tmpDir = path.join(require('os').tmpdir(), 'pseudo-repl');
        if (!fs.existsSync(tmpDir)) {
            fs.mkdirSync(tmpDir, { recursive: true });
        }
        const tmpFile = path.join(tmpDir, `repl_${Date.now()}.pseudo`);
        
        // Wrap expression in print if it's a simple expression
        let wrappedCode = code;
        const trimmed = code.trim();
        if (!trimmed.startsWith('let ') && 
            !trimmed.startsWith('const ') &&
            !trimmed.startsWith('fn ') &&
            !trimmed.startsWith('if ') &&
            !trimmed.startsWith('while ') &&
            !trimmed.startsWith('for ') &&
            !trimmed.startsWith('print') &&
            !trimmed.startsWith('match ') &&
            !trimmed.includes('\n')) {
            // It's an expression, wrap in print
            wrappedCode = `print(${code})`;
        }

        fs.writeFileSync(tmpFile, wrappedCode);

        const proc = spawn(vmPath!, [tmpFile]);
        
        proc.stdout.on('data', (data: Buffer) => {
            const output = data.toString();
            output.split('\n').forEach((line, i, arr) => {
                if (line || i < arr.length - 1) {
                    writer.fire(`\x1b[0m${line}\r\n`);
                }
            });
        });

        proc.stderr.on('data', (data: Buffer) => {
            const output = data.toString();
            output.split('\n').forEach((line, i, arr) => {
                if (line || i < arr.length - 1) {
                    writer.fire(`\x1b[31m${line}\x1b[0m\r\n`);
                }
            });
        });

        proc.on('close', () => {
            // Clean up temp file
            try {
                fs.unlinkSync(tmpFile);
            } catch (e) {
                // Ignore
            }
            onComplete();
        });
    }

    replTerminal = vscode.window.createTerminal({
        name: 'Pseudocode REPL',
        pty
    });

    replTerminal.show();
}

export function registerRepl(context: vscode.ExtensionContext, findVmPath: () => Promise<string | null>): void {
    const replCommand = vscode.commands.registerCommand('pseudocode.repl', async () => {
        const vmPath = await findVmPath();
        await startRepl(vmPath);
    });

    context.subscriptions.push(replCommand);
}
