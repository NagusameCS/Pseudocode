/*
 * Pseudocode VS Code Extension - WASM Runtime Integration
 * 
 * This module provides the WASM-based Pseudocode runtime for the VS Code extension.
 * It replaces the need for platform-specific native binaries, making the extension
 * truly cross-platform with a single .vsix file.
 * 
 * The WASM runtime compiles Pseudocode to WebAssembly and executes it using
 * the built-in V8 WASM engine in Node.js/Electron.
 */

import * as vscode from 'vscode';
import * as path from 'path';

/**
 * Result of running Pseudocode code.
 */
export interface RunResult {
    success: boolean;
    output: string;
    errors: string[];
    value?: unknown;
}

/**
 * Configuration for the WASM runtime.
 */
export interface WasmRuntimeConfig {
    debug?: boolean;
    outputChannel?: vscode.OutputChannel;
    timeout?: number;
}

// Lazy-loaded WASM module
let wasmModulePromise: Promise<typeof import('@pseudocode/wasm')> | null = null;

async function getWasmModule() {
    if (!wasmModulePromise) {
        wasmModulePromise = import('@pseudocode/wasm');
    }
    return wasmModulePromise;
}

/**
 * WASM-based Pseudocode runtime for VS Code.
 */
export class PseudocodeWasmRuntime {
    private outputChannel: vscode.OutputChannel;
    private config: WasmRuntimeConfig;
    private outputBuffer: string[] = [];
    
    constructor(config: WasmRuntimeConfig = {}) {
        this.config = config;
        this.outputChannel = config.outputChannel || vscode.window.createOutputChannel('Pseudocode WASM');
    }
    
    /**
     * Initialize the WASM runtime.
     */
    async initialize(): Promise<void> {
        // Pre-load the WASM module
        await getWasmModule();
    }
    
    /**
     * Run Pseudocode source code.
     */
    async runSource(source: string): Promise<RunResult> {
        this.outputBuffer = [];
        
        try {
            const wasm = await getWasmModule();
            
            const result = await wasm.run(source, {
                debug: this.config.debug,
                timeout: this.config.timeout
            });
            
            // Capture output
            if (result.output) {
                this.outputChannel.append(result.output);
            }
            
            return result;
        } catch (error) {
            return {
                success: false,
                output: this.outputBuffer.join(''),
                errors: [error instanceof Error ? error.message : String(error)]
            };
        }
    }
    
    /**
     * Run a Pseudocode file.
     */
    async runFile(filePath: string): Promise<RunResult> {
        try {
            const uri = vscode.Uri.file(filePath);
            const content = await vscode.workspace.fs.readFile(uri);
            const source = new TextDecoder().decode(content);
            
            this.outputChannel.appendLine(`Running: ${path.basename(filePath)}`);
            this.outputChannel.appendLine('─'.repeat(50));
            
            const result = await this.runSource(source);
            
            if (!result.success) {
                this.outputChannel.appendLine('\n─── Errors ───');
                for (const error of result.errors) {
                    this.outputChannel.appendLine(`  ✗ ${error}`);
                }
            }
            
            this.outputChannel.appendLine('─'.repeat(50));
            this.outputChannel.appendLine(result.success ? '✓ Completed' : '✗ Failed');
            
            return result;
        } catch (error) {
            const errorMsg = error instanceof Error ? error.message : String(error);
            this.outputChannel.appendLine(`Error: ${errorMsg}`);
            
            return {
                success: false,
                output: '',
                errors: [errorMsg]
            };
        }
    }
    
    /**
     * Compile Pseudocode to WASM bytes (for analysis/export).
     */
    async compileToWasm(source: string): Promise<{ wasm: Uint8Array; errors: string[] }> {
        const wasm = await getWasmModule();
        return wasm.compileToWasm(source);
    }
    
    /**
     * Show the output channel.
     */
    showOutput(): void {
        this.outputChannel.show();
    }
    
    /**
     * Clear the output channel.
     */
    clearOutput(): void {
        this.outputChannel.clear();
        this.outputBuffer = [];
    }
    
    /**
     * Dispose of resources.
     */
    dispose(): void {
        // Nothing to dispose for WASM runtime
    }
}

/**
 * Global WASM runtime instance.
 */
let globalRuntime: PseudocodeWasmRuntime | null = null;

/**
 * Get the global WASM runtime instance.
 */
export async function getWasmRuntime(): Promise<PseudocodeWasmRuntime> {
    if (!globalRuntime) {
        globalRuntime = new PseudocodeWasmRuntime();
        await globalRuntime.initialize();
    }
    return globalRuntime;
}

/**
 * Dispose of the global runtime.
 */
export function disposeWasmRuntime(): void {
    if (globalRuntime) {
        globalRuntime.dispose();
        globalRuntime = null;
    }
}

/**
 * Register WASM-based commands for VS Code.
 */
export function registerWasmCommands(context: vscode.ExtensionContext): void {
    // Run current file with WASM
    const runFileCommand = vscode.commands.registerCommand('pseudocode.runWasm', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'pseudocode') {
            vscode.window.showErrorMessage('No Pseudocode file is open');
            return;
        }
        
        const runtime = await getWasmRuntime();
        runtime.clearOutput();
        runtime.showOutput();
        
        // Use document text (may be unsaved)
        const result = await runtime.runSource(editor.document.getText());
        
        if (!result.success) {
            vscode.window.showErrorMessage(`Execution failed: ${result.errors[0]}`);
        }
    });
    
    // Run selection with WASM
    const runSelectionCommand = vscode.commands.registerCommand('pseudocode.runSelectionWasm', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showErrorMessage('No editor is open');
            return;
        }
        
        const selection = editor.document.getText(editor.selection);
        if (!selection) {
            vscode.window.showErrorMessage('No text selected');
            return;
        }
        
        const runtime = await getWasmRuntime();
        runtime.showOutput();
        
        const result = await runtime.runSource(selection);
        
        if (!result.success) {
            vscode.window.showErrorMessage(`Execution failed: ${result.errors[0]}`);
        }
    });
    
    // Show compiled WASM
    const showWasmCommand = vscode.commands.registerCommand('pseudocode.showWasm', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'pseudocode') {
            vscode.window.showErrorMessage('No Pseudocode file is open');
            return;
        }
        
        const runtime = await getWasmRuntime();
        const result = await runtime.compileToWasm(editor.document.getText());
        
        if (result.errors.length > 0) {
            vscode.window.showErrorMessage(`Compilation errors: ${result.errors[0]}`);
            return;
        }
        
        // Show WASM info
        const wasmSize = result.wasm.length;
        const message = `Compiled WASM: ${wasmSize} bytes`;
        vscode.window.showInformationMessage(message);
        
        // Optionally save WASM file
        const save = await vscode.window.showQuickPick(['Save WASM file', 'Cancel'], {
            placeHolder: 'Would you like to save the WASM file?'
        });
        
        if (save === 'Save WASM file') {
            const uri = await vscode.window.showSaveDialog({
                defaultUri: vscode.Uri.file(editor.document.fileName.replace(/\.(pseudo|psc)$/, '.wasm')),
                filters: { 'WebAssembly': ['wasm'] }
            });
            
            if (uri) {
                await vscode.workspace.fs.writeFile(uri, result.wasm);
                vscode.window.showInformationMessage(`Saved: ${uri.fsPath}`);
            }
        }
    });
    
    context.subscriptions.push(runFileCommand, runSelectionCommand, showWasmCommand);
}
