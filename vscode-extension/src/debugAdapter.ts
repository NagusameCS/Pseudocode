/*
 * Pseudocode Debug Adapter
 * Implements the Debug Adapter Protocol for step-through debugging
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import { spawn, ChildProcess } from 'child_process';

// ============ Debug Session ============

export class PseudocodeDebugSession implements vscode.DebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(
        session: vscode.DebugSession
    ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
        return new vscode.DebugAdapterInlineImplementation(
            new InlinePseudocodeDebugAdapter()
        );
    }
}

// ============ Inline Debug Adapter ============

class InlinePseudocodeDebugAdapter implements vscode.DebugAdapter {
    private sendMessage = new vscode.EventEmitter<vscode.DebugProtocolMessage>();
    private process: ChildProcess | null = null;
    private breakpoints: Map<string, number[]> = new Map();
    private currentLine: number = 0;
    private currentFile: string = '';
    private sourceLines: string[] = [];
    private variables: Map<string, any> = new Map();
    private paused: boolean = false;
    private sequenceNumber: number = 0;

    onDidSendMessage: vscode.Event<vscode.DebugProtocolMessage> = this.sendMessage.event;

    handleMessage(message: vscode.DebugProtocolMessage): void {
        const request = message as any;

        switch (request.command) {
            case 'initialize':
                this.handleInitialize(request);
                break;
            case 'launch':
                this.handleLaunch(request);
                break;
            case 'setBreakpoints':
                this.handleSetBreakpoints(request);
                break;
            case 'threads':
                this.handleThreads(request);
                break;
            case 'stackTrace':
                this.handleStackTrace(request);
                break;
            case 'scopes':
                this.handleScopes(request);
                break;
            case 'variables':
                this.handleVariables(request);
                break;
            case 'continue':
                this.handleContinue(request);
                break;
            case 'next':
                this.handleNext(request);
                break;
            case 'stepIn':
                this.handleStepIn(request);
                break;
            case 'stepOut':
                this.handleStepOut(request);
                break;
            case 'pause':
                this.handlePause(request);
                break;
            case 'disconnect':
                this.handleDisconnect(request);
                break;
            case 'configurationDone':
                this.handleConfigurationDone(request);
                break;
            default:
                this.sendResponse(request, {});
        }
    }

    private sendResponse(request: any, body: any = {}): void {
        this.sendMessage.fire({
            type: 'response',
            seq: ++this.sequenceNumber,
            request_seq: request.seq,
            success: true,
            command: request.command,
            body
        });
    }

    private sendEvent(event: string, body: any = {}): void {
        this.sendMessage.fire({
            type: 'event',
            seq: ++this.sequenceNumber,
            event,
            body
        });
    }

    private handleInitialize(request: any): void {
        this.sendResponse(request, {
            supportsConfigurationDoneRequest: true,
            supportsFunctionBreakpoints: false,
            supportsConditionalBreakpoints: false,
            supportsHitConditionalBreakpoints: false,
            supportsEvaluateForHovers: true,
            supportsStepBack: false,
            supportsSetVariable: false,
            supportsRestartFrame: false,
            supportsGotoTargetsRequest: false,
            supportsStepInTargetsRequest: false,
            supportsCompletionsRequest: false,
            supportsModulesRequest: false,
            supportsExceptionOptions: false,
            supportsValueFormattingOptions: false,
            supportsExceptionInfoRequest: false,
            supportTerminateDebuggee: true,
            supportSuspendDebuggee: true,
            supportsDelayedStackTraceLoading: false,
            supportsLoadedSourcesRequest: false,
            supportsLogPoints: false,
            supportsTerminateThreadsRequest: false,
            supportsSetExpression: false,
            supportsTerminateRequest: true
        });

        this.sendEvent('initialized');
    }

    private async handleLaunch(request: any): Promise<void> {
        const program = request.arguments.program;
        const cwd = request.arguments.cwd || path.dirname(program);
        const vmPath = request.arguments.vmPath || 'pseudo';
        const args = request.arguments.args || [];

        this.currentFile = program;

        // Read source for line tracking
        try {
            const content = fs.readFileSync(program, 'utf8');
            this.sourceLines = content.split('\n');
        } catch (e) {
            this.sendEvent('output', {
                category: 'stderr',
                output: `Failed to read source file: ${program}\n`
            });
        }

        // For now, we'll run in "simulated debug" mode
        // A full implementation would require VM support for debug protocol
        this.sendResponse(request);

        // Start the process
        const isWindows = process.platform === 'win32';
        this.process = spawn(vmPath, ['--debug', program, ...args], {
            cwd,
            stdio: ['pipe', 'pipe', 'pipe'],
            shell: isWindows
        });

        this.process.stdout?.on('data', (data: Buffer) => {
            const output = data.toString();

            // Parse debug output from VM
            const lineMatch = output.match(/\[DEBUG:LINE:(\d+)\]/);
            if (lineMatch) {
                this.currentLine = parseInt(lineMatch[1]);
                this.checkBreakpoint();
            } else {
                this.sendEvent('output', {
                    category: 'stdout',
                    output
                });
            }
        });

        this.process.stderr?.on('data', (data: Buffer) => {
            this.sendEvent('output', {
                category: 'stderr',
                output: data.toString()
            });
        });

        this.process.on('close', (code: number) => {
            this.sendEvent('output', {
                category: 'console',
                output: `\nProgram exited with code ${code}\n`
            });
            this.sendEvent('terminated');
        });

        this.sendEvent('output', {
            category: 'console',
            output: `Debugging: ${path.basename(program)}\n`
        });

        // If no debug mode support, run normally
        if (!this.breakpoints.size) {
            // No breakpoints, just run
            this.sendEvent('continued', { threadId: 1, allThreadsContinued: true });
        }
    }

    private handleSetBreakpoints(request: any): void {
        const source = request.arguments.source;
        const breakpointRequests = request.arguments.breakpoints || [];

        const filePath = source.path;
        const lines = breakpointRequests.map((bp: any) => bp.line);

        this.breakpoints.set(filePath, lines);

        const breakpoints = lines.map((line: number) => ({
            verified: true,
            line,
            id: line
        }));

        this.sendResponse(request, { breakpoints });
    }

    private handleThreads(request: any): void {
        this.sendResponse(request, {
            threads: [
                { id: 1, name: 'Main Thread' }
            ]
        });
    }

    private handleStackTrace(request: any): void {
        const frames = [
            {
                id: 1,
                name: 'main',
                source: {
                    name: path.basename(this.currentFile),
                    path: this.currentFile
                },
                line: this.currentLine,
                column: 0
            }
        ];

        this.sendResponse(request, {
            stackFrames: frames,
            totalFrames: frames.length
        });
    }

    private handleScopes(request: any): void {
        this.sendResponse(request, {
            scopes: [
                {
                    name: 'Local',
                    variablesReference: 1,
                    expensive: false
                },
                {
                    name: 'Global',
                    variablesReference: 2,
                    expensive: false
                }
            ]
        });
    }

    private handleVariables(request: any): void {
        // Simulated variables for now
        const vars: any[] = [];

        this.variables.forEach((value, name) => {
            vars.push({
                name,
                value: String(value),
                variablesReference: 0
            });
        });

        // If no real variables, show placeholders
        if (vars.length === 0) {
            vars.push({
                name: '(no variables)',
                value: '',
                variablesReference: 0
            });
        }

        this.sendResponse(request, { variables: vars });
    }

    private handleContinue(request: any): void {
        this.paused = false;
        this.process?.stdin?.write('c\n');
        this.sendResponse(request, { allThreadsContinued: true });
        this.sendEvent('continued', { threadId: 1, allThreadsContinued: true });
    }

    private handleNext(request: any): void {
        this.process?.stdin?.write('n\n');
        this.sendResponse(request);
        // Wait for next line event
    }

    private handleStepIn(request: any): void {
        this.process?.stdin?.write('s\n');
        this.sendResponse(request);
    }

    private handleStepOut(request: any): void {
        this.process?.stdin?.write('o\n');
        this.sendResponse(request);
    }

    private handlePause(request: any): void {
        this.paused = true;
        this.sendResponse(request);
        this.sendEvent('stopped', {
            reason: 'pause',
            threadId: 1,
            allThreadsStopped: true
        });
    }

    private handleConfigurationDone(request: any): void {
        this.sendResponse(request);
        // Start execution
        this.sendEvent('continued', { threadId: 1 });
    }

    private handleDisconnect(request: any): void {
        if (this.process) {
            this.process.kill();
            this.process = null;
        }
        this.sendResponse(request);
    }

    private checkBreakpoint(): void {
        const breakpointsForFile = this.breakpoints.get(this.currentFile);
        if (breakpointsForFile?.includes(this.currentLine)) {
            this.paused = true;
            this.sendEvent('stopped', {
                reason: 'breakpoint',
                threadId: 1,
                allThreadsStopped: true
            });
        }
    }

    dispose(): void {
        if (this.process) {
            this.process.kill();
        }
    }
}

// ============ Debug Configuration Provider ============

export class PseudocodeDebugConfigurationProvider implements vscode.DebugConfigurationProvider {
    resolveDebugConfiguration(
        folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration,
        token?: vscode.CancellationToken
    ): vscode.ProviderResult<vscode.DebugConfiguration> {
        // If no launch.json, create default config
        if (!config.type && !config.request && !config.name) {
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.languageId === 'pseudocode') {
                config.type = 'pseudocode';
                config.name = 'Debug Pseudocode';
                config.request = 'launch';
                config.program = '${file}';
                config.cwd = '${workspaceFolder}';
            }
        }

        if (!config.program) {
            return vscode.window.showInformationMessage('Cannot find a program to debug').then(_ => {
                return undefined;
            });
        }

        return config;
    }
}

// ============ Registration ============

export function registerDebugger(context: vscode.ExtensionContext): void {
    // Register debug configuration provider
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider(
            'pseudocode',
            new PseudocodeDebugConfigurationProvider()
        )
    );

    // Register debug adapter descriptor factory
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory(
            'pseudocode',
            new PseudocodeDebugSession()
        )
    );
}
