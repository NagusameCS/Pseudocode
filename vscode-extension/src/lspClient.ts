/*
 * Pseudocode Language Client
 * 
 * VS Code extension client that connects to the Pseudocode Language Server.
 * This provides a clean separation between the extension UI and the language
 * intelligence features.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as path from 'path';
import * as vscode from 'vscode';

import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export async function activateLspClient(context: vscode.ExtensionContext): Promise<LanguageClient | undefined> {
    // Path to the server module
    const serverModule = context.asAbsolutePath(
        path.join('out', 'server', 'server.js')
    );

    // Debug options for the server
    const debugOptions = { execArgv: ['--nolazy', '--inspect=6009'] };

    // Server options - run the server as a separate Node.js process
    const serverOptions: ServerOptions = {
        run: { 
            module: serverModule, 
            transport: TransportKind.ipc 
        },
        debug: {
            module: serverModule,
            transport: TransportKind.ipc,
            options: debugOptions
        }
    };

    // Document selectors for Pseudocode files
    const documentSelector = [
        { scheme: 'file', language: 'pseudocode' },
        { scheme: 'untitled', language: 'pseudocode' }
    ];

    // Client options
    const clientOptions: LanguageClientOptions = {
        documentSelector,
        synchronize: {
            // Synchronize configuration settings
            configurationSection: 'pseudocode',
            // Watch for file changes in .pseudo files
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{pseudo,psc,pseudoh,psch,pseudocode}')
        },
        initializationOptions: {
            // Pass initial settings to server
            diagnosticsEnabled: vscode.workspace.getConfiguration('pseudocode').get('diagnostics.enabled', true),
            inlayHintsEnabled: vscode.workspace.getConfiguration('pseudocode').get('inlayHints.enabled', true),
            codeLensEnabled: vscode.workspace.getConfiguration('pseudocode').get('codeLens.enabled', true)
        },
        middleware: {
            // Custom middleware to enhance LSP responses
            provideCompletionItem: async (document, position, context, token, next) => {
                const result = await next(document, position, context, token);
                // Could add additional completion items here
                return result;
            }
        }
    };

    // Create the language client
    client = new LanguageClient(
        'pseudocodeLanguageServer',
        'Pseudocode Language Server',
        serverOptions,
        clientOptions
    );

    // Register custom commands that the server might need
    context.subscriptions.push(
        vscode.commands.registerCommand('pseudocode.extractFunction', async (uri: string, range: vscode.Range) => {
            // Handle extract function refactoring
            const edit = new vscode.WorkspaceEdit();
            const document = await vscode.workspace.openTextDocument(vscode.Uri.parse(uri));
            const selectedText = document.getText(range);
            
            // Ask for function name
            const name = await vscode.window.showInputBox({
                prompt: 'Enter function name',
                value: 'extractedFunction'
            });
            
            if (!name) return;
            
            // Create the function
            const params: string[] = [];
            const fnText = `fn ${name}(${params.join(', ')})\n    ${selectedText}\nend\n\n`;
            
            // Insert at the beginning of the file (or before current function)
            edit.insert(document.uri, new vscode.Position(0, 0), fnText);
            
            // Replace selection with function call
            edit.replace(document.uri, range, `${name}()`);
            
            await vscode.workspace.applyEdit(edit);
        }),

        vscode.commands.registerCommand('pseudocode.showReferences', async (uri: string, position: vscode.Position, locations: vscode.Location[]) => {
            await vscode.commands.executeCommand(
                'editor.action.showReferences',
                vscode.Uri.parse(uri),
                position,
                locations
            );
        })
    );

    // Start the client (also launches the server)
    try {
        await client.start();
        console.log('Pseudocode Language Server started');
    } catch (error) {
        console.error('Failed to start Pseudocode Language Server:', error);
        vscode.window.showWarningMessage(
            'Pseudocode Language Server failed to start. Some features may be limited.'
        );
        return undefined;
    }

    return client;
}

export async function deactivateLspClient(): Promise<void> {
    if (client) {
        await client.stop();
        client = undefined;
    }
}

export function getClient(): LanguageClient | undefined {
    return client;
}
