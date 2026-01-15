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
import { exec, spawn } from 'child_process';
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
let useWasmRuntime = true; // Default to WASM for cross-platform

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

    // Register run command (uses WASM by default, falls back to native)
    const runCommand = vscode.commands.registerCommand('pseudocode.run', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'pseudocode') {
            vscode.window.showErrorMessage('No Pseudocode file is open');
            return;
        }

        // Use WASM runtime by default
        if (useWasmRuntime) {
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
                // Fall back to native if WASM fails
                console.warn('WASM runtime failed, falling back to native:', wasmError);
            }
        }

        // Fall back to native binary
        await editor.document.save();

        const filePath = editor.document.fileName;
        const vmPath = await findVmPath(context.extensionPath);

        if (!vmPath) {
            vscode.window.showErrorMessage('Pseudocode VM not found. Try enabling WASM runtime in settings.');
            return;
        }

        runPseudocode(vmPath, filePath);
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

    // Register REPL - pass a wrapper that includes extension path
    const findVmPathWithContext = () => findVmPath(context.extensionPath);
    registerRepl(context, findVmPathWithContext);

    // Show activation message
    console.log('Pseudocode extension activated');
}

// Get the bundled VM binary name for the current platform
function getBundledVmName(): string {
    const platform = process.platform; // 'darwin', 'linux', 'win32'
    const arch = process.arch; // 'arm64', 'x64'
    
    if (platform === 'win32') {
        return arch === 'arm64' ? 'pseudo-win32-arm64.exe' : 'pseudo-win32-x64.exe';
    } else if (platform === 'darwin') {
        return arch === 'arm64' ? 'pseudo-darwin-arm64' : 'pseudo-darwin-x64';
    } else {
        return arch === 'arm64' ? 'pseudo-linux-arm64' : 'pseudo-linux-x64';
    }
}

async function findVmPath(extensionPath?: string): Promise<string | null> {
    const config = vscode.workspace.getConfiguration('pseudocode');
    const configuredPath = config.get<string>('vmPath');

    // Check configured path first
    if (configuredPath && fs.existsSync(configuredPath)) {
        return configuredPath;
    }
    
    // Check for bundled VM binary in extension (PRIORITY - works out of the box)
    if (extensionPath) {
        const bundledName = getBundledVmName();
        const bundledVm = path.join(extensionPath, 'bin', bundledName);
        if (fs.existsSync(bundledVm)) {
            // Ensure it's executable on Unix
            if (process.platform !== 'win32') {
                try {
                    fs.chmodSync(bundledVm, 0o755);
                } catch (e) {
                    // Ignore chmod errors
                }
            }
            return bundledVm;
        }
        
        // Also check for generic 'pseudo' name as fallback
        const genericVm = path.join(extensionPath, 'bin', process.platform === 'win32' ? 'pseudo.exe' : 'pseudo');
        if (fs.existsSync(genericVm)) {
            if (process.platform !== 'win32') {
                try {
                    fs.chmodSync(genericVm, 0o755);
                } catch (e) {}
            }
            return genericVm;
        }
    }

    // On Windows, look for .exe extension
    const isWindows = process.platform === 'win32';
    const exeNames = isWindows ? ['pseudo.exe', 'pseudo'] : ['pseudo'];
    
    // Helper to check multiple paths
    const checkPaths = (basePath: string): string | null => {
        for (const exeName of exeNames) {
            // Check cvm/pseudo (main VM location)
            const vmInCvm = path.join(basePath, 'cvm', exeName);
            if (fs.existsSync(vmInCvm)) {
                return vmInCvm;
            }

            // Check cvm/pseudo_debug (debug build)
            const vmDebugName = isWindows ? exeName.replace('pseudo', 'pseudo_debug') : 'pseudo_debug';
            const vmDebug = path.join(basePath, 'cvm', vmDebugName);
            if (fs.existsSync(vmDebug)) {
                return vmDebug;
            }

            // Check bin/pseudo (installed location)
            const vmInBin = path.join(basePath, 'bin', exeName);
            if (fs.existsSync(vmInBin)) {
                return vmInBin;
            }

            // Check root pseudo
            const vmInRoot = path.join(basePath, exeName);
            if (fs.existsSync(vmInRoot)) {
                return vmInRoot;
            }

            // Check build/pseudo (CMake build)
            const vmInBuild = path.join(basePath, 'build', exeName);
            if (fs.existsSync(vmInBuild)) {
                return vmInBuild;
            }
        }
        return null;
    };

    // Check workspace folders for compiled VM
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        for (const folder of workspaceFolders) {
            const result = checkPaths(folder.uri.fsPath);
            if (result) return result;
        }
    }
    
    // Check from the active file's directory (traverse up to find project root)
    const activeEditor = vscode.window.activeTextEditor;
    if (activeEditor) {
        let dir = path.dirname(activeEditor.document.fileName);
        // Traverse up looking for cvm folder
        for (let i = 0; i < 5; i++) {
            const result = checkPaths(dir);
            if (result) return result;
            const parent = path.dirname(dir);
            if (parent === dir) break;
            dir = parent;
        }
    }

    // Check common installation paths (Unix)
    if (!isWindows) {
        const commonPaths = [
            '/usr/local/bin/pseudo',
            '/usr/bin/pseudo',
            path.join(process.env.HOME || '', '.local', 'bin', 'pseudo'),
            path.join(process.env.HOME || '', 'bin', 'pseudo')
        ];

        for (const p of commonPaths) {
            if (fs.existsSync(p)) {
                return p;
            }
        }
    }

    // Check if pseudo is in PATH
    return new Promise((resolve) => {
        const cmd = isWindows ? 'where pseudo.exe' : 'which pseudo';
        exec(cmd, (error, stdout) => {
            if (!error && stdout.trim()) {
                resolve(stdout.trim().split('\n')[0].trim());
            } else {
                // Try without .exe on Windows as fallback
                if (isWindows) {
                    exec('where pseudo', (error2, stdout2) => {
                        if (!error2 && stdout2.trim()) {
                            resolve(stdout2.trim().split('\n')[0].trim());
                        } else {
                            resolve(null);
                        }
                    });
                } else {
                    resolve(null);
                }
            }
        });
    });
}

function runPseudocode(vmPath: string, filePath: string): void {
    outputChannel.clear();
    outputChannel.show(true);

    const config = vscode.workspace.getConfiguration('pseudocode');
    const showTime = config.get<boolean>('showExecutionTime', true);
    const enableJIT = config.get<boolean>('enableJIT', true);
    const debugMode = config.get<boolean>('debugMode', false);

    outputChannel.appendLine(`Running: ${path.basename(filePath)}`);
    outputChannel.appendLine('─'.repeat(50));

    const startTime = Date.now();

    // Build command line arguments
    const args: string[] = [];

    if (enableJIT) {
        args.push('-j');  // Enable JIT (default, but explicit)
    } else {
        args.push('-i');  // Interpreter only mode (more stable)
    }

    if (debugMode) {
        args.push('-d');  // Debug mode
    }

    args.push(filePath);

    // On Windows, need to handle paths with spaces properly
    const isWindows = process.platform === 'win32';

    const childProcess = spawn(vmPath, args, {
        cwd: path.dirname(filePath),
        shell: isWindows,  // Use shell on Windows to handle paths properly
        windowsVerbatimArguments: false
    });

    // Track for cleanup
    activeChildProcesses.add(childProcess);

    childProcess.stdout.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    childProcess.stderr.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    childProcess.on('close', (code: number) => {
        // Remove from tracking
        activeChildProcesses.delete(childProcess);
        
        outputChannel.appendLine('');
        outputChannel.appendLine('─'.repeat(50));

        if (code === 0) {
            if (showTime) {
                const elapsed = Date.now() - startTime;
                outputChannel.appendLine(`✓ Completed in ${elapsed}ms`);
            } else {
                outputChannel.appendLine('✓ Completed successfully');
            }
        } else {
            outputChannel.appendLine(`✗ Exited with code ${code}`);
        }
    });

    childProcess.on('error', (err: Error) => {
        outputChannel.appendLine(`Error: ${err.message}`);
    });
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
