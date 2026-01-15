/*
 * Pseudocode Language - Advanced Features
 * Code Lens, Go-to-Definition, Rename, Semantic Tokens, Status Bar
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';

// Helper to escape special regex characters
function escapeRegex(str: string): string {
    return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// ============ Symbol Index ============

interface SymbolInfo {
    name: string;
    kind: 'function' | 'variable' | 'parameter';
    line: number;
    column: number;
    endLine?: number;
    params?: string[];
    doc?: string;
}

interface FileSymbols {
    functions: Map<string, SymbolInfo>;
    variables: Map<string, SymbolInfo[]>;
    references: Map<string, vscode.Location[]>;
}

interface CachedSymbols {
    version: number;
    symbols: FileSymbols;
}

const symbolCache = new Map<string, CachedSymbols>();

// Export for cleanup on document close
export function clearSymbolCache(uri: string): void {
    symbolCache.delete(uri);
}

function parseDocument(document: vscode.TextDocument): FileSymbols {
    // Check if we have a valid cached version
    const cached = symbolCache.get(document.uri.toString());
    if (cached && cached.version === document.version) {
        return cached.symbols;
    }
    
    const symbols: FileSymbols = {
        functions: new Map(),
        variables: new Map(),
        references: new Map()
    };

    try {
        const text = document.getText();
        const lines = text.split('\n');

    let currentFunction: SymbolInfo | null = null;
    let functionStartLine = 0;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];

        // Function definitions
        const fnMatch = line.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/);
        if (fnMatch) {
            const name = fnMatch[1];
            const params = fnMatch[2].split(',').map(p => p.trim()).filter(p => p);

            // Get doc comment above
            let doc = '';
            if (i > 0 && lines[i - 1].trim().startsWith('//')) {
                doc = lines[i - 1].trim().substring(2).trim();
            }

            currentFunction = {
                name,
                kind: 'function',
                line: i,
                column: line.indexOf(name),
                params,
                doc
            };
            functionStartLine = i;
            symbols.functions.set(name, currentFunction);

            // Add parameters as variables
            params.forEach((param, idx) => {
                const paramName = param.split(':')[0].trim();
                if (!symbols.variables.has(paramName)) {
                    symbols.variables.set(paramName, []);
                }
                symbols.variables.get(paramName)!.push({
                    name: paramName,
                    kind: 'parameter',
                    line: i,
                    column: line.indexOf(paramName)
                });
            });
        }

        // End of function
        if (line.trim() === 'end' && currentFunction) {
            currentFunction.endLine = i;
            currentFunction = null;
        }

        // Variable declarations
        const letMatch = line.match(/let\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*=/);
        if (letMatch) {
            const name = letMatch[1];
            if (!symbols.variables.has(name)) {
                symbols.variables.set(name, []);
            }
            symbols.variables.get(name)!.push({
                name,
                kind: 'variable',
                line: i,
                column: line.indexOf(name)
            });
        }

        // Function calls (references)
        const callMatches = line.matchAll(/([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g);
        for (const match of callMatches) {
            const name = match[1];
            if (!['if', 'while', 'for', 'fn', 'match'].includes(name)) {
                if (!symbols.references.has(name)) {
                    symbols.references.set(name, []);
                }
                symbols.references.get(name)!.push(
                    new vscode.Location(
                        document.uri,
                        new vscode.Position(i, match.index!)
                    )
                );
            }
        }
    }

    symbolCache.set(document.uri.toString(), { version: document.version, symbols });
    } catch (error) {
        console.error('Parse document error:', error);
    }
    return symbols;
}

function getSymbols(document: vscode.TextDocument): FileSymbols {
    return parseDocument(document);  // parseDocument now handles caching
}

// ============ Code Lens Provider ============

export class PseudocodeCodeLensProvider implements vscode.CodeLensProvider {
    private _onDidChangeCodeLenses = new vscode.EventEmitter<void>();
    public readonly onDidChangeCodeLenses = this._onDidChangeCodeLenses.event;

    provideCodeLenses(document: vscode.TextDocument): vscode.CodeLens[] {
        try {
            const lenses: vscode.CodeLens[] = [];
            const symbols = parseDocument(document);

            for (const [name, info] of symbols.functions) {
                const range = new vscode.Range(info.line, 0, info.line, 0);

                // Run button
                lenses.push(new vscode.CodeLens(range, {
                    title: '▶ Run',
                    command: 'pseudocode.runFunction',
                    arguments: [document.uri, name]
                }));

                // Reference count
                const refs = symbols.references.get(name) || [];
                const refCount = refs.length;
                lenses.push(new vscode.CodeLens(range, {
                    title: `${refCount} reference${refCount !== 1 ? 's' : ''}`,
                    command: 'pseudocode.showReferences',
                    arguments: [document.uri, new vscode.Position(info.line, info.column), refs]
                }));

                // Show params
                if (info.params && info.params.length > 0) {
                    lenses.push(new vscode.CodeLens(range, {
                        title: `⚙ ${info.params.join(', ')}`,
                        command: ''
                    }));
                }
            }

            return lenses;
        } catch (error) {
            console.error('Code lens error:', error);
            return [];
        }
    }
}

// ============ Go to Definition ============

export class PseudocodeDefinitionProvider implements vscode.DefinitionProvider {
    provideDefinition(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.ProviderResult<vscode.Definition> {
        try {
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) return undefined;

            const word = document.getText(wordRange);
            const symbols = getSymbols(document);

            // Check functions
            const fn = symbols.functions.get(word);
            if (fn) {
                return new vscode.Location(
                    document.uri,
                    new vscode.Position(fn.line, fn.column)
                );
            }

            // Check variables
            const vars = symbols.variables.get(word);
            if (vars && vars.length > 0) {
                // Return the first definition
                const v = vars[0];
                return new vscode.Location(
                    document.uri,
                    new vscode.Position(v.line, v.column)
                );
            }

            return undefined;
        } catch (error) {
            console.error('Definition provider error:', error);
            return undefined;
        }
    }
}

// ============ Find All References ============

export class PseudocodeReferenceProvider implements vscode.ReferenceProvider {
    provideReferences(
        document: vscode.TextDocument,
        position: vscode.Position,
        _context: vscode.ReferenceContext
    ): vscode.ProviderResult<vscode.Location[]> {
        try {
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) return [];

            const word = document.getText(wordRange);
            
            // Escape special regex characters to prevent crashes
            const escapedWord = word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            
            const symbols = getSymbols(document);
            const locations: vscode.Location[] = [];

            // Add definition
            const fn = symbols.functions.get(word);
            if (fn) {
                locations.push(new vscode.Location(
                    document.uri,
                    new vscode.Position(fn.line, fn.column)
                ));
            }

            const vars = symbols.variables.get(word);
            if (vars) {
                vars.forEach(v => {
                    locations.push(new vscode.Location(
                        document.uri,
                        new vscode.Position(v.line, v.column)
                    ));
                });
            }

            // Add references
            const refs = symbols.references.get(word);
            if (refs) {
                locations.push(...refs);
            }

            // Scan for variable usages
            const text = document.getText();
            const regex = new RegExp(`\\b${escapedWord}\\b`, 'g');
            let match;
            while ((match = regex.exec(text)) !== null) {
                const pos = document.positionAt(match.index);
                const existing = locations.find(l =>
                    l.range.start.line === pos.line && l.range.start.character === pos.character
                );
                if (!existing) {
                    locations.push(new vscode.Location(document.uri, pos));
                }
            }

            return locations;
        } catch (error) {
            console.error('Reference provider error:', error);
            return [];
        }
    }
}

// ============ Rename Provider ============

export class PseudocodeRenameProvider implements vscode.RenameProvider {
    provideRenameEdits(
        document: vscode.TextDocument,
        position: vscode.Position,
        newName: string
    ): vscode.ProviderResult<vscode.WorkspaceEdit> {
        try {
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) return undefined;

            const oldName = document.getText(wordRange);

            // Validate new name
            if (!/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(newName)) {
                throw new Error('Invalid identifier name');
            }

            const edit = new vscode.WorkspaceEdit();
            const text = document.getText();

            // Escape special regex characters and find all occurrences
            const escapedOldName = oldName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            const regex = new RegExp(`\\b${escapedOldName}\\b`, 'g');
            let match;
            while ((match = regex.exec(text)) !== null) {
                const startPos = document.positionAt(match.index);
                const endPos = document.positionAt(match.index + oldName.length);
                edit.replace(document.uri, new vscode.Range(startPos, endPos), newName);
            }

            return edit;
        } catch (error) {
            console.error('Rename provider error:', error);
            return undefined;
        }
    }

    prepareRename(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.ProviderResult<vscode.Range> {
        try {
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) {
                throw new Error('Cannot rename at this position');
            }

            const word = document.getText(wordRange);
            const symbols = getSymbols(document);

            // Only allow renaming user-defined symbols
            if (symbols.functions.has(word) || symbols.variables.has(word)) {
                return wordRange;
            }

            // Check if it's a local reference
            const refs = symbols.references.get(word);
            if (refs && refs.length > 0) {
                return wordRange;
            }

            throw new Error('Cannot rename built-in or undefined symbol');
        } catch (error) {
            // Let the error propagate for prepareRename so VS Code shows the message
            throw error;
        }
    }
}

// ============ Document Highlight ============

// Cache for document highlights to prevent lag on every cursor movement
interface HighlightCache {
    version: number;
    word: string;
    highlights: vscode.DocumentHighlight[];
}
const highlightCache = new Map<string, HighlightCache>();

export class PseudocodeDocumentHighlightProvider implements vscode.DocumentHighlightProvider {
    provideDocumentHighlights(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.ProviderResult<vscode.DocumentHighlight[]> {
        try {
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) return [];

            const word = document.getText(wordRange);
            
            // Skip very short words or keywords to reduce noise
            if (word.length < 2) return [];
            
            const uri = document.uri.toString();
            const cached = highlightCache.get(uri);
            
            // Return cached if version and word match
            if (cached && cached.version === document.version && cached.word === word) {
                return cached.highlights;
            }
            
            const text = document.getText();
            const highlights: vscode.DocumentHighlight[] = [];
            const escapedWord = escapeRegex(word);
            const regex = new RegExp(`\\b${escapedWord}\\b`, 'g');
            let match;
            while ((match = regex.exec(text)) !== null) {
                const startPos = document.positionAt(match.index);
                const endPos = document.positionAt(match.index + word.length);
                const range = new vscode.Range(startPos, endPos);

                // Check if it's a definition
                const line = document.lineAt(startPos.line).text;
                const isDefinition = line.match(new RegExp(`(fn|let)\\s+${escapedWord}\\b`));

                highlights.push(new vscode.DocumentHighlight(
                    range,
                    isDefinition
                        ? vscode.DocumentHighlightKind.Write
                        : vscode.DocumentHighlightKind.Read
                ));
            }

            // Cache the result
            highlightCache.set(uri, { version: document.version, word, highlights });
            return highlights;
        } catch (error) {
            console.error('Document highlight error:', error);
            return [];
        }
    }
}

// ============ Folding Range Provider ============

// Cache for folding ranges
interface FoldingRangeCache {
    version: number;
    ranges: vscode.FoldingRange[];
}
const foldingRangeCache = new Map<string, FoldingRangeCache>();

export class PseudocodeFoldingRangeProvider implements vscode.FoldingRangeProvider {
    provideFoldingRanges(document: vscode.TextDocument): vscode.ProviderResult<vscode.FoldingRange[]> {
        try {
            const uri = document.uri.toString();
            const cached = foldingRangeCache.get(uri);
            
            // Return cached if version matches
            if (cached && cached.version === document.version) {
                return cached.ranges;
            }
            
            const text = document.getText();
            const ranges: vscode.FoldingRange[] = [];
            const lines = text.split('\n');
            const stack: { kind: string; line: number }[] = [];

            for (let i = 0; i < lines.length; i++) {
                const line = lines[i].trim();

                // Block starters
                if (line.match(/^fn\s+/) ||
                    (line.match(/^if\s+/) && line.includes('then')) ||
                    line.match(/^while\s+.*\s+do/) ||
                    line.match(/^for\s+.*\s+do/) ||
                    line.match(/^match\s+/)) {
                    stack.push({ kind: 'block', line: i });
                }

                // End
                if (line === 'end' && stack.length > 0) {
                    const start = stack.pop()!;
                    ranges.push(new vscode.FoldingRange(start.line, i, vscode.FoldingRangeKind.Region));
                }

                // Comment blocks
                if (line.startsWith('//') && i > 0 && lines[i - 1].trim().startsWith('//')) {
                    // Already part of block
                } else if (line.startsWith('//')) {
                    // Start of potential comment block
                    let end = i;
                    while (end + 1 < lines.length && lines[end + 1].trim().startsWith('//')) {
                        end++;
                    }
                    if (end > i) {
                        ranges.push(new vscode.FoldingRange(i, end, vscode.FoldingRangeKind.Comment));
                    }
                }
            }

            // Cache the result
            foldingRangeCache.set(uri, { version: document.version, ranges });
            return ranges;
        } catch (error) {
            console.error('Folding range error:', error);
            return [];
        }
    }
}

// ============ Semantic Tokens ============

const tokenTypes = ['function', 'variable', 'parameter', 'keyword', 'string', 'number', 'comment', 'operator', 'type'];
const tokenModifiers = ['declaration', 'definition', 'readonly', 'modification'];

export const semanticTokensLegend = new vscode.SemanticTokensLegend(tokenTypes, tokenModifiers);

// Cache for semantic tokens
interface SemanticTokenCache {
    version: number;
    tokens: vscode.SemanticTokens;
}
const semanticTokensCache = new Map<string, SemanticTokenCache>();

export class PseudocodeSemanticTokensProvider implements vscode.DocumentSemanticTokensProvider {
    provideDocumentSemanticTokens(document: vscode.TextDocument): vscode.ProviderResult<vscode.SemanticTokens> {
        try {
            const uri = document.uri.toString();
            const cached = semanticTokensCache.get(uri);
            
            // Return cached if version matches
            if (cached && cached.version === document.version) {
                return cached.tokens;
            }
            
            const text = document.getText();
            
            const builder = new vscode.SemanticTokensBuilder(semanticTokensLegend);
            const lines = text.split('\n');
            
            // Limit lines to process for very long files
            const maxLines = Math.min(lines.length, 2000);

        for (let i = 0; i < maxLines; i++) {
            const line = lines[i];

            // Function definitions
            const fnDefMatch = line.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
            if (fnDefMatch) {
                const col = line.indexOf(fnDefMatch[1]);
                builder.push(i, col, fnDefMatch[1].length, 0, 1); // function + definition
            }

            // Function calls
            const callMatches = line.matchAll(/([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g);
            for (const match of callMatches) {
                if (!['if', 'while', 'for', 'fn', 'match'].includes(match[1]) &&
                    !line.trim().startsWith('fn ')) {
                    builder.push(i, match.index!, match[1].length, 0, 0); // function
                }
            }

            // Variable declarations
            const letMatch = line.match(/let\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
            if (letMatch) {
                const col = line.indexOf(letMatch[1], line.indexOf('let'));
                builder.push(i, col, letMatch[1].length, 1, 1); // variable + definition
            }
        }

            const tokens = builder.build();
            semanticTokensCache.set(uri, { version: document.version, tokens });
            return tokens;
        } catch (error) {
            console.error('Semantic tokens error:', error);
            return new vscode.SemanticTokensBuilder(semanticTokensLegend).build();
        }
    }
}

// ============ Workspace Symbol Provider ============

export class PseudocodeWorkspaceSymbolProvider implements vscode.WorkspaceSymbolProvider {
    async provideWorkspaceSymbols(query: string): Promise<vscode.SymbolInformation[]> {
        try {
            const symbols: vscode.SymbolInformation[] = [];
            const files = await vscode.workspace.findFiles('**/*.{pseudo,psc,pseudocode}', '**/node_modules/**', 100);

            for (const file of files) {
                try {
                    const content = await fs.promises.readFile(file.fsPath, 'utf8');
                    const lines = content.split('\n');

                    for (let i = 0; i < lines.length; i++) {
                        const fnMatch = lines[i].match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
                        if (fnMatch) {
                            const name = fnMatch[1];
                            if (name.toLowerCase().includes(query.toLowerCase())) {
                                symbols.push(new vscode.SymbolInformation(
                                    name,
                                    vscode.SymbolKind.Function,
                                    path.basename(file.fsPath),
                                    new vscode.Location(file, new vscode.Position(i, 0))
                                ));
                            }
                        }
                    }
                } catch {
                    // Ignore read errors
                }
            }

            return symbols;
        } catch (error) {
            console.error('Workspace symbol error:', error);
            return [];
        }
    }
}

// ============ Status Bar ============

let statusBarItem: vscode.StatusBarItem;

export function createStatusBar(): vscode.StatusBarItem {
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    statusBarItem.command = 'pseudocode.run';
    updateStatusBar();
    return statusBarItem;
}

export function updateStatusBar(document?: vscode.TextDocument): void {
    if (!statusBarItem) return;

    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'pseudocode') {
        statusBarItem.hide();
        return;
    }

    const doc = document || editor.document;
    const symbols = getSymbols(doc);

    const fnCount = symbols.functions.size;
    const varCount = symbols.variables.size;
    const lineCount = doc.lineCount;

    statusBarItem.text = `$(symbol-function) ${fnCount} $(symbol-variable) ${varCount} $(list-flat) ${lineCount}`;
    statusBarItem.tooltip = `Pseudocode: ${fnCount} functions, ${varCount} variables, ${lineCount} lines\nClick to run`;
    statusBarItem.show();
}

// ============ Code Actions ============

export class PseudocodeCodeActionProvider implements vscode.CodeActionProvider {
    public static readonly providedCodeActionKinds = [
        vscode.CodeActionKind.QuickFix,
        vscode.CodeActionKind.Refactor
    ];

    provideCodeActions(
        document: vscode.TextDocument,
        range: vscode.Range,
        context: vscode.CodeActionContext
    ): vscode.ProviderResult<vscode.CodeAction[]> {
        try {
            const actions: vscode.CodeAction[] = [];

            // Quick fixes for diagnostics
            for (const diagnostic of context.diagnostics) {
                if (diagnostic.message.includes("Did you mean")) {
                    const match = diagnostic.message.match(/Did you mean '(\w+)'/);
                    if (match) {
                        const fix = new vscode.CodeAction(
                            `Replace with '${match[1]}'`,
                            vscode.CodeActionKind.QuickFix
                        );
                        fix.edit = new vscode.WorkspaceEdit();
                        fix.edit.replace(document.uri, diagnostic.range, match[1]);
                        fix.diagnostics = [diagnostic];
                    fix.isPreferred = true;
                    actions.push(fix);
                }
            }

            if (diagnostic.message.includes("missing 'end'")) {
                const fix = new vscode.CodeAction(
                    `Add missing 'end'`,
                    vscode.CodeActionKind.QuickFix
                );
                fix.edit = new vscode.WorkspaceEdit();
                const insertLine = document.lineCount;
                fix.edit.insert(document.uri, new vscode.Position(insertLine, 0), '\nend\n');
                fix.diagnostics = [diagnostic];
                actions.push(fix);
            }
        }

        // Refactoring: Extract function
        const selection = range;
        if (!selection.isEmpty) {
            const selectedText = document.getText(selection);
            if (selectedText.trim().length > 0 && !selectedText.includes('fn ')) {
                const extract = new vscode.CodeAction(
                    'Extract to function',
                    vscode.CodeActionKind.RefactorExtract
                );
                extract.command = {
                    command: 'pseudocode.extractFunction',
                    title: 'Extract to function',
                    arguments: [document, selection, selectedText]
                };
                actions.push(extract);
            }
        }

        // Add function documentation
        const line = document.lineAt(range.start.line);
        if (line.text.trim().startsWith('fn ') && range.start.line > 0) {
            const prevLine = document.lineAt(range.start.line - 1);
            if (!prevLine.text.trim().startsWith('//')) {
                const addDoc = new vscode.CodeAction(
                    'Add documentation comment',
                    vscode.CodeActionKind.RefactorRewrite
                );
                addDoc.edit = new vscode.WorkspaceEdit();
                const fnMatch = line.text.match(/fn\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
                const fnName = fnMatch ? fnMatch[1] : 'function';
                addDoc.edit.insert(
                    document.uri,
                    new vscode.Position(range.start.line, 0),
                    `// ${fnName}: Description\n`
                );
                actions.push(addDoc);
            }
        }

            return actions;
        } catch (error) {
            console.error('Code action error:', error);
            return [];
        }
    }
}

// ============ Register All ============

export function registerAdvancedFeatures(context: vscode.ExtensionContext): void {
    const config = vscode.workspace.getConfiguration('pseudocode');
    const selector: vscode.DocumentSelector = { language: 'pseudocode', scheme: 'file' };

    // Each feature has its own setting, all disabled by default

    // Code Lens
    if (config.get<boolean>('codeLens.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerCodeLensProvider(selector, new PseudocodeCodeLensProvider())
        );
    }

    // Go to Definition
    if (config.get<boolean>('goToDefinition.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerDefinitionProvider(selector, new PseudocodeDefinitionProvider())
        );
    }

    // Find References
    if (config.get<boolean>('findReferences.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerReferenceProvider(selector, new PseudocodeReferenceProvider())
        );
    }

    // Rename
    if (config.get<boolean>('rename.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerRenameProvider(selector, new PseudocodeRenameProvider())
        );
    }

    // Document Highlight
    if (config.get<boolean>('documentHighlight.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerDocumentHighlightProvider(selector, new PseudocodeDocumentHighlightProvider())
        );
    }

    // Folding - always enabled, it's lightweight
    context.subscriptions.push(
        vscode.languages.registerFoldingRangeProvider(selector, new PseudocodeFoldingRangeProvider())
    );

    // Semantic Highlighting
    if (config.get<boolean>('semanticHighlighting.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerDocumentSemanticTokensProvider(
                selector,
                new PseudocodeSemanticTokensProvider(),
                semanticTokensLegend
            )
        );
    }

    // Workspace Symbols (Ctrl+T) - always enabled, only runs on explicit search
    context.subscriptions.push(
        vscode.languages.registerWorkspaceSymbolProvider(new PseudocodeWorkspaceSymbolProvider())
    );

    // Code Actions - always enabled, lightweight
    context.subscriptions.push(
        vscode.languages.registerCodeActionsProvider(
            selector,
            new PseudocodeCodeActionProvider(),
            { providedCodeActionKinds: PseudocodeCodeActionProvider.providedCodeActionKinds }
        )
    );

    // Status Bar
    if (config.get<boolean>('statusBar.enabled', false)) {
        const statusBar = createStatusBar();
        context.subscriptions.push(statusBar);
    }

    // Debounced status bar updates to prevent lag
    let statusBarTimeout: NodeJS.Timeout | undefined;
    
    // Update status bar on document change (debounced)
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(() => {
            if (statusBarTimeout) clearTimeout(statusBarTimeout);
            statusBarTimeout = setTimeout(() => updateStatusBar(), 200);
        })
    );
    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument((e) => {
            if (e.document.languageId === 'pseudocode') {
                // Don't parse on every keystroke - let providers handle their own caching
                if (statusBarTimeout) clearTimeout(statusBarTimeout);
                statusBarTimeout = setTimeout(() => updateStatusBar(e.document), 500);
            }
        })
    );

    // Commands
    context.subscriptions.push(
        vscode.commands.registerCommand('pseudocode.runFunction', async (uri: vscode.Uri, fnName: string) => {
            const vmPath = await findVmPath();
            if (!vmPath) {
                vscode.window.showErrorMessage('Pseudocode VM not found');
                return;
            }
            // Run the file (function execution would require runtime support)
            vscode.commands.executeCommand('pseudocode.run');
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('pseudocode.showReferences', async (uri: vscode.Uri, pos: vscode.Position, refs: vscode.Location[]) => {
            vscode.commands.executeCommand('editor.action.showReferences', uri, pos, refs);
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('pseudocode.extractFunction', async (document: vscode.TextDocument, range: vscode.Range, text: string) => {
            const name = await vscode.window.showInputBox({
                prompt: 'Enter function name',
                value: 'extracted_function'
            });
            if (!name) return;

            const edit = new vscode.WorkspaceEdit();

            // Find function insertion point (before first function or at top)
            let insertLine = 0;
            const lines = document.getText().split('\n');
            for (let i = 0; i < lines.length; i++) {
                if (lines[i].trim().startsWith('fn ')) {
                    insertLine = i;
                    break;
                }
            }

            // Create function
            const fnText = `fn ${name}()\n    ${text.trim()}\nend\n\n`;
            edit.insert(document.uri, new vscode.Position(insertLine, 0), fnText);

            // Replace selection with function call
            edit.replace(document.uri, range, `${name}()`);

            await vscode.workspace.applyEdit(edit);
        })
    );

    console.log('Pseudocode advanced features registered');
}

// Helper function (also defined in extension.ts, shared here)
async function findVmPath(): Promise<string | null> {
    const config = vscode.workspace.getConfiguration('pseudocode');
    const configuredPath = config.get<string>('vmPath');

    if (configuredPath && fs.existsSync(configuredPath)) {
        return configuredPath;
    }

    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        for (const folder of workspaceFolders) {
            const vmInCvm = path.join(folder.uri.fsPath, 'cvm', 'pseudo');
            if (fs.existsSync(vmInCvm)) {
                return vmInCvm;
            }
        }
    }

    return null;
}
