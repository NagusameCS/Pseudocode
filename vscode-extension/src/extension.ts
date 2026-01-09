/*
 * Pseudocode Language - VS Code Extension
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
    createDiagnostics
} from './languageFeatures';
import { registerDebugger } from './debugAdapter';
import { registerRepl } from './repl';

let outputChannel: vscode.OutputChannel;
let diagnosticCollection: vscode.DiagnosticCollection;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Pseudocode');
    diagnosticCollection = vscode.languages.createDiagnosticCollection('pseudocode');

    const selector: vscode.DocumentSelector = { language: 'pseudocode', scheme: 'file' };

    // ============ Language Features ============

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

    // Real-time diagnostics
    const updateDiagnostics = (document: vscode.TextDocument) => {
        if (document.languageId === 'pseudocode') {
            createDiagnostics(document, diagnosticCollection);
        }
    };

    // Update diagnostics on document change
    vscode.workspace.onDidChangeTextDocument(event => {
        updateDiagnostics(event.document);
    }, null, context.subscriptions);

    // Update diagnostics on document open
    vscode.workspace.onDidOpenTextDocument(document => {
        updateDiagnostics(document);
    }, null, context.subscriptions);

    // Update diagnostics for all open pseudocode files
    vscode.workspace.textDocuments.forEach(updateDiagnostics);

    // Clear diagnostics on close
    vscode.workspace.onDidCloseTextDocument(document => {
        diagnosticCollection.delete(document.uri);
    }, null, context.subscriptions);

    context.subscriptions.push(
        completionProvider,
        hoverProvider,
        symbolProvider,
        signatureProvider,
        formatterProvider,
        diagnosticCollection
    );

    // ============ Commands ============

    // Register run command
    const runCommand = vscode.commands.registerCommand('pseudocode.run', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'pseudocode') {
            vscode.window.showErrorMessage('No Pseudocode file is open');
            return;
        }

        // Save the file first
        await editor.document.save();

        const filePath = editor.document.fileName;
        const vmPath = await findVmPath();

        if (!vmPath) {
            vscode.window.showErrorMessage('Pseudocode VM (pseudo) not found. Please set pseudocode.vmPath in settings or ensure the VM is built.');
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

    context.subscriptions.push(runCommand, buildCommand, outputChannel);

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

    // Register REPL
    registerRepl(context, findVmPath);

    // Show activation message
    console.log('Pseudocode extension activated');
}

async function findVmPath(): Promise<string | null> {
    const config = vscode.workspace.getConfiguration('pseudocode');
    const configuredPath = config.get<string>('vmPath');

    // Check configured path first
    if (configuredPath && fs.existsSync(configuredPath)) {
        return configuredPath;
    }

    // Check workspace folders for compiled VM
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        for (const folder of workspaceFolders) {
            // Check cvm/pseudo
            const vmInCvm = path.join(folder.uri.fsPath, 'cvm', 'pseudo');
            if (fs.existsSync(vmInCvm)) {
                return vmInCvm;
            }

            // Check root pseudo
            const vmInRoot = path.join(folder.uri.fsPath, 'pseudo');
            if (fs.existsSync(vmInRoot)) {
                return vmInRoot;
            }
        }
    }

    // Check if pseudo is in PATH
    return new Promise((resolve) => {
        exec('which pseudo', (error, stdout) => {
            if (!error && stdout.trim()) {
                resolve(stdout.trim());
            } else {
                resolve(null);
            }
        });
    });
}

function runPseudocode(vmPath: string, filePath: string): void {
    outputChannel.clear();
    outputChannel.show(true);

    const config = vscode.workspace.getConfiguration('pseudocode');
    const showTime = config.get<boolean>('showExecutionTime', true);

    outputChannel.appendLine(`Running: ${path.basename(filePath)}`);
    outputChannel.appendLine('─'.repeat(50));

    const startTime = Date.now();

    const process = spawn(vmPath, [filePath], {
        cwd: path.dirname(filePath)
    });

    process.stdout.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    process.stderr.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    process.on('close', (code: number) => {
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

    process.on('error', (err: Error) => {
        outputChannel.appendLine(`Error: ${err.message}`);
    });
}

function buildVm(cvmPath: string): void {
    outputChannel.clear();
    outputChannel.show(true);

    outputChannel.appendLine('Building Pseudocode VM...');
    outputChannel.appendLine('─'.repeat(50));

    const process = spawn('make', ['clean', 'pgo'], {
        cwd: cvmPath,
        shell: true
    });

    process.stdout.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    process.stderr.on('data', (data: Buffer) => {
        outputChannel.append(data.toString());
    });

    process.on('close', (code: number) => {
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

export function deactivate() { }
