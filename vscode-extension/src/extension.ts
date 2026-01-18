/*
 * Pseudocode Language - VS Code Extension
 *
 * Full LSP-based language support with fallback to embedded providers.
 * The LSP server runs as pure JavaScript - no architecture-specific binaries needed.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import { spawn } from 'child_process';
import {
    PseudocodeCompletionProvider,
    PseudocodeHoverProvider,
    PseudocodeDocumentSymbolProvider,
    PseudocodeSignatureHelpProvider,
    PseudocodeDocumentFormatter,
    createDiagnostics,
    prewarmCaches,
    clearDocumentSymbolCache
} from './languageFeatures';
import { registerDebugger } from './debugAdapter';
import { registerRepl } from './repl';
import { registerAdvancedFeatures, clearSymbolCache } from './advancedFeatures';
import { registerExtraFeatures } from './extraFeatures';
import { activateLspClient, deactivateLspClient } from './lspClient';
import { registerWasmCommands, disposeWasmRuntime, getWasmRuntime } from './wasm';

let outputChannel: vscode.OutputChannel;
let diagnosticCollection: vscode.DiagnosticCollection;
let diagnosticsTimeout: NodeJS.Timeout | undefined;
let activeChildProcesses: Set<import('child_process').ChildProcess> = new Set();
let useLspServer = false;

export async function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Pseudocode');
    diagnosticCollection = vscode.languages.createDiagnosticCollection('pseudocode');

    const selector: vscode.DocumentSelector = { language: 'pseudocode', scheme: 'file' };

    // ============ LSP Server Mode ============
    // Try to start the LSP server for full language intelligence
    // The LSP server is pure JavaScript - works on all platforms without recompilation
    const config = vscode.workspace.getConfiguration('pseudocode');
    const lspEnabled = config.get<boolean>('lsp.enabled', true);
    
    if (lspEnabled) {
        try {
            const serverPath = path.join(context.extensionPath, 'out', 'server', 'server.js');
            if (fs.existsSync(serverPath)) {
                const client = await activateLspClient(context);
                if (client) {
                    useLspServer = true;
                    console.log('Pseudocode LSP server activated - full language intelligence enabled');
                }
            }
        } catch (error) {
            console.warn('LSP server not available, falling back to embedded providers:', error);
        }
    }

    // ============ Fallback: Embedded Language Features ============
    // Only register embedded providers if LSP is not running
    if (!useLspServer) {
        // Pre-warm all caches immediately to avoid first-use lag
        prewarmCaches();

        // Autocomplete
        const completionProvider = vscode.languages.registerCompletionItemProvider(
            selector,
            new PseudocodeCompletionProvider(),
            '.', '(' // Trigger characters
        );

        // Hover documentation
        const hoverProvider = vscode.languages.registerHoverProvider(
            selector,
            new PseudocodeHoverProvider()
        );

        // Document outline (symbols)
        const symbolProvider = vscode.languages.registerDocumentSymbolProvider(
            selector,
            new PseudocodeDocumentSymbolProvider()
        );

        // Signature help (function parameters)
        const signatureProvider = vscode.languages.registerSignatureHelpProvider(
            selector,
            new PseudocodeSignatureHelpProvider(),
            '(', ','
        );

        // Document formatter
        const formatterProvider = vscode.languages.registerDocumentFormattingEditProvider(
            selector,
            new PseudocodeDocumentFormatter()
        );

        // Debounced diagnostics
        const DIAGNOSTICS_DELAY = 500;
        
        const updateDiagnosticsDebounced = (document: vscode.TextDocument) => {
            if (document.languageId !== 'pseudocode') return;
            
            const config = vscode.workspace.getConfiguration('pseudocode');
            if (!config.get<boolean>('diagnostics.enabled', false)) {
                return;
            }
            
            if (diagnosticsTimeout) {
                clearTimeout(diagnosticsTimeout);
            }
            
            diagnosticsTimeout = setTimeout(() => {
                createDiagnostics(document, diagnosticCollection);
            }, DIAGNOSTICS_DELAY);
        };
        
        const updateDiagnosticsImmediate = (document: vscode.TextDocument) => {
            if (document.languageId === 'pseudocode') {
                const config = vscode.workspace.getConfiguration('pseudocode');
                if (!config.get<boolean>('diagnostics.enabled', false)) {
                    return;
                }
                updateDiagnosticsDebounced(document);
            }
        };

        vscode.workspace.onDidChangeTextDocument(event => {
            updateDiagnosticsDebounced(event.document);
        }, null, context.subscriptions);

        vscode.workspace.onDidOpenTextDocument(document => {
            updateDiagnosticsImmediate(document);
        }, null, context.subscriptions);

        vscode.workspace.textDocuments.forEach(updateDiagnosticsImmediate);

        vscode.workspace.onDidCloseTextDocument(document => {
            diagnosticCollection.delete(document.uri);
            // Clear symbol caches to prevent memory leaks
            const uri = document.uri.toString();
            clearSymbolCache(uri);
            clearDocumentSymbolCache(uri);
        }, null, context.subscriptions);

        context.subscriptions.push(
            completionProvider,
            hoverProvider,
            symbolProvider,
            signatureProvider,
            formatterProvider,
            diagnosticCollection
        );

        // ============ Advanced Features (fallback mode) ============
        registerAdvancedFeatures(context);

        // ============ Extra Features (fallback mode) ============
        registerExtraFeatures(context);
    }

    // ============ Commands ============

    // Check runtime preference
    const runtimeConfig = vscode.workspace.getConfiguration('pseudocode');
    useWasmRuntime = runtimeConfig.get<boolean>('useWasmRuntime', true);

    // Register WASM commands (always available)
    registerWasmCommands(context);

    // Register run command (uses WASM runtime exclusively)
    const runCommand = vscode.commands.registerCommand('pseudocode.run', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'pseudocode') {
            vscode.window.showErrorMessage('No Pseudocode file is open');
            return;
        }

        // Use WASM runtime - no fallback to native binaries
        try {
            const runtime = await getWasmRuntime();
            runtime.clearOutput();
            runtime.showOutput();
            
            const result = await runtime.runSource(editor.document.getText());
            
            if (!result.success) {
                vscode.window.showErrorMessage(`Execution failed: ${result.errors[0]}`);
            }
            return;
        } catch (wasmError) {
            const errorMsg = wasmError instanceof Error ? wasmError.message : String(wasmError);
            vscode.window.showErrorMessage(`WASM runtime error: ${errorMsg}`);
            console.error('WASM runtime failed:', wasmError);
        }
    });

    // Register build command
    const buildCommand = vscode.commands.registerCommand('pseudocode.build', async () => {
        const workspaceFolders = vscode.workspace.workspaceFolders;
        if (!workspaceFolders) {
            vscode.window.showErrorMessage('No workspace folder open');
            return;
        }

        // Look for cvm directory with Makefile
        for (const folder of workspaceFolders) {
            const cvmPath = path.join(folder.uri.fsPath, 'cvm');
            const makefilePath = path.join(cvmPath, 'Makefile');

            if (fs.existsSync(makefilePath)) {
                buildVm(cvmPath);
                return;
            }
        }

        vscode.window.showErrorMessage('Could not find cvm/Makefile in workspace');
    });

    // Register stop command
    const stopCommand = vscode.commands.registerCommand('pseudocode.stop', () => {
        if (activeChildProcesses.size === 0) {
            vscode.window.showInformationMessage('No Pseudocode program is running');
            return;
        }

        for (const proc of activeChildProcesses) {
            try {
                proc.kill('SIGTERM');
            } catch (e) {
                // Process may have already exited
            }
        }
        activeChildProcesses.clear();
        outputChannel.appendLine('');
        outputChannel.appendLine('─'.repeat(50));
        outputChannel.appendLine('⏹ Program stopped by user');
        vscode.window.showInformationMessage('Pseudocode program stopped');
    });

    context.subscriptions.push(runCommand, buildCommand, stopCommand, outputChannel);

    // Register task provider
    const taskProvider = vscode.tasks.registerTaskProvider('pseudocode', {
        provideTasks: () => {
            return [];
        },
        resolveTask: (task: vscode.Task) => {
            return task;
        }
    });

    context.subscriptions.push(taskProvider);

    // Register debugger
    registerDebugger(context);

    // Register REPL (uses WASM runtime)
    registerRepl(context);

    // Show activation message
    console.log('Pseudocode extension activated');
}

function buildVm(cvmPath: string): void {
    outputChannel.clear();
    outputChannel.show(true);

    outputChannel.appendLine('Building Pseudocode VM...');
    outputChannel.appendLine('─'.repeat(50));

    const buildProcess = spawn('make', ['clean', 'pgo'], {
        cwd: cvmPath,
        shell: true
    });

    buildProcess.stdout.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    buildProcess.stderr.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    buildProcess.on('close', (code: number) => {
        outputChannel.appendLine('');
        outputChannel.appendLine('─'.repeat(50));

        if (code === 0) {
            outputChannel.appendLine('✓ Build completed successfully');
            vscode.window.showInformationMessage('Pseudocode VM built successfully!');
        } else {
            outputChannel.appendLine(`✗ Build failed with code ${code}`);
            vscode.window.showErrorMessage('Failed to build Pseudocode VM');
        }
    });
}

export async function deactivate() {
    // Stop LSP client if running
    if (useLspServer) {
        await deactivateLspClient();
    }
    
    // Dispose WASM runtime
    disposeWasmRuntime();
    
    // Clean up diagnostics timeout
    if (diagnosticsTimeout) {
        clearTimeout(diagnosticsTimeout);
        diagnosticsTimeout = undefined;
    }
    
    // Kill any active child processes
    for (const proc of activeChildProcesses) {
        try {
            proc.kill();
        } catch (e) {
            // Ignore errors during cleanup
        }
    }
    activeChildProcesses.clear();
    
    // Dispose output channel
    if (outputChannel) {
        outputChannel.dispose();
    }
    
    // Clear diagnostics
    if (diagnosticCollection) {
        diagnosticCollection.clear();
        diagnosticCollection.dispose();
    }
}
