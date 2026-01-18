/*
 * Pseudocode REPL
 * Interactive Read-Eval-Print Loop for Pseudocode
 * Uses WASM runtime for cross-platform compatibility
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';
import { getWasmRuntime } from './wasm';

let replTerminal: vscode.Terminal | undefined;

export async function startRepl(): Promise<void> {
    // Check if REPL is already running
    if (replTerminal) {
        replTerminal.show();
        return;
    }

    // Create a pseudo-terminal for the REPL
    const writeEmitter = new vscode.EventEmitter<string>();
    let inputBuffer = '';
    let history: string[] = [];
    let historyIndex = -1;

    const pty: vscode.Pseudoterminal = {
        onDidWrite: writeEmitter.event,
        open: () => {
            writeEmitter.fire('Pseudocode REPL v2.3\r\n');
            writeEmitter.fire('Type expressions to evaluate. Type "exit" to quit.\r\n');
            writeEmitter.fire('>>> ');
        },
        close: () => {
            replTerminal = undefined;
        },
        handleInput: async (data: string) => {
            // Handle special keys
            if (data === '\r') { // Enter
                writeEmitter.fire('\r\n');
                const input = inputBuffer.trim();
                inputBuffer = '';

                if (input === 'exit' || input === 'quit') {
                    replTerminal?.dispose();
                    replTerminal = undefined;
                    return;
                }

                if (input === 'clear') {
                    writeEmitter.fire('\x1b[2J\x1b[H');
                    writeEmitter.fire('>>> ');
                    return;
                }

                if (input === 'help') {
                    writeEmitter.fire('Commands:\r\n');
                    writeEmitter.fire('  exit, quit  - Exit the REPL\r\n');
                    writeEmitter.fire('  clear       - Clear the screen\r\n');
                    writeEmitter.fire('  help        - Show this help\r\n');
                    writeEmitter.fire('\r\n>>> ');
                    return;
                }

                if (input) {
                    history.push(input);
                    historyIndex = history.length;

                    try {
                        const runtime = await getWasmRuntime();
                        const result = await runtime.runSource(input);
                        if (result.output) {
                            // Replace \n with \r\n for terminal
                            const output = result.output.replace(/\n/g, '\r\n');
                            writeEmitter.fire(output);
                            if (!output.endsWith('\r\n')) {
                                writeEmitter.fire('\r\n');
                            }
                        }
                        if (!result.success && result.errors.length > 0) {
                            writeEmitter.fire(`\x1b[31mError: ${result.errors[0]}\x1b[0m\r\n`);
                        }
                    } catch (err) {
                        writeEmitter.fire(`\x1b[31mError: ${err}\x1b[0m\r\n`);
                    }
                }

                writeEmitter.fire('>>> ');
            } else if (data === '\x7f') { // Backspace
                if (inputBuffer.length > 0) {
                    inputBuffer = inputBuffer.slice(0, -1);
                    writeEmitter.fire('\b \b');
                }
            } else if (data === '\x1b[A') { // Up arrow
                if (historyIndex > 0) {
                    historyIndex--;
                    // Clear current line
                    writeEmitter.fire('\r\x1b[K>>> ');
                    inputBuffer = history[historyIndex];
                    writeEmitter.fire(inputBuffer);
                }
            } else if (data === '\x1b[B') { // Down arrow
                if (historyIndex < history.length - 1) {
                    historyIndex++;
                    writeEmitter.fire('\r\x1b[K>>> ');
                    inputBuffer = history[historyIndex];
                    writeEmitter.fire(inputBuffer);
                } else if (historyIndex === history.length - 1) {
                    historyIndex = history.length;
                    writeEmitter.fire('\r\x1b[K>>> ');
                    inputBuffer = '';
                }
            } else if (data === '\x03') { // Ctrl+C
                writeEmitter.fire('^C\r\n>>> ');
                inputBuffer = '';
            } else if (data.charCodeAt(0) >= 32) { // Printable characters
                inputBuffer += data;
                writeEmitter.fire(data);
            }
        }
    };

    replTerminal = vscode.window.createTerminal({
        name: 'Pseudocode REPL',
        pty
    });

    replTerminal.show();
}

export function registerRepl(context: vscode.ExtensionContext): void {
    const replCommand = vscode.commands.registerCommand('pseudocode.repl', startRepl);
    context.subscriptions.push(replCommand);
}

export function disposeRepl(): void {
    if (replTerminal) {
        replTerminal.dispose();
        replTerminal = undefined;
    }
}
