/*
 * Pseudocode Language Server
 * 
 * A full Language Server Protocol (LSP) implementation for the Pseudocode language.
 * Written in TypeScript/Node.js for cross-platform compatibility - no architecture-specific
 * binaries required.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import {
    createConnection,
    TextDocuments,
    ProposedFeatures,
    InitializeParams,
    TextDocumentSyncKind,
    InitializeResult,
    CompletionItem,
    CompletionItemKind,
    TextDocumentPositionParams,
    Hover,
    MarkupKind,
    Definition,
    Location,
    Range,
    Position,
    DocumentSymbol,
    SymbolKind,
    SignatureHelp,
    SignatureInformation,
    ParameterInformation,
    DocumentFormattingParams,
    TextEdit,
    Diagnostic,
    DiagnosticSeverity,
    ReferenceParams,
    RenameParams,
    WorkspaceEdit,
    PrepareRenameParams,
    FoldingRange,
    FoldingRangeKind,
    CodeLens,
    CodeAction,
    CodeActionKind,
    CodeActionParams,
    SemanticTokensBuilder,
    SemanticTokensLegend,
    SemanticTokens,
    InlayHint,
    InlayHintKind,
    DocumentHighlight,
    DocumentHighlightKind,
    CallHierarchyItem,
    CallHierarchyIncomingCall,
    CallHierarchyOutgoingCall,
    SelectionRange,
    WorkspaceSymbol,
    DidChangeConfigurationNotification
} from 'vscode-languageserver/node';

import { TextDocument } from 'vscode-languageserver-textdocument';
import { PseudocodeAnalyzer, ParsedDocument, SymbolInfo, DiagnosticInfo } from './analyzer';
import { BUILTINS, KEYWORDS, BUILTIN_SIGNATURES } from './builtins';

// Create connection using Node's IPC
const connection = createConnection(ProposedFeatures.all);

// Text document manager
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

// Analyzer instance
const analyzer = new PseudocodeAnalyzer();

// Configuration
let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;

interface PseudocodeSettings {
    diagnosticsEnabled: boolean;
    maxDiagnostics: number;
    inlayHintsEnabled: boolean;
    codeLensEnabled: boolean;
    formatOnType: boolean;
}

const defaultSettings: PseudocodeSettings = {
    diagnosticsEnabled: true,
    maxDiagnostics: 100,
    inlayHintsEnabled: true,
    codeLensEnabled: true,
    formatOnType: false
};

let globalSettings: PseudocodeSettings = defaultSettings;
const documentSettings: Map<string, Thenable<PseudocodeSettings>> = new Map();

// Semantic tokens legend
const tokenTypes = ['function', 'variable', 'parameter', 'keyword', 'string', 'number', 'comment', 'operator', 'class', 'enum', 'enumMember'];
const tokenModifiers = ['declaration', 'definition', 'readonly', 'static', 'async'];

const semanticTokensLegend: SemanticTokensLegend = {
    tokenTypes,
    tokenModifiers
};

connection.onInitialize((params: InitializeParams): InitializeResult => {
    const capabilities = params.capabilities;

    hasConfigurationCapability = !!(
        capabilities.workspace && !!capabilities.workspace.configuration
    );
    hasWorkspaceFolderCapability = !!(
        capabilities.workspace && !!capabilities.workspace.workspaceFolders
    );

    return {
        capabilities: {
            textDocumentSync: TextDocumentSyncKind.Incremental,
            completionProvider: {
                resolveProvider: true,
                triggerCharacters: ['.', '(', ',']
            },
            hoverProvider: true,
            signatureHelpProvider: {
                triggerCharacters: ['(', ','],
                retriggerCharacters: [',']
            },
            definitionProvider: true,
            referencesProvider: true,
            documentHighlightProvider: true,
            documentSymbolProvider: true,
            workspaceSymbolProvider: true,
            codeActionProvider: {
                codeActionKinds: [
                    CodeActionKind.QuickFix,
                    CodeActionKind.Refactor,
                    CodeActionKind.RefactorExtract,
                    CodeActionKind.Source
                ]
            },
            codeLensProvider: {
                resolveProvider: true
            },
            documentFormattingProvider: true,
            documentRangeFormattingProvider: true,
            documentOnTypeFormattingProvider: {
                firstTriggerCharacter: '\n',
                moreTriggerCharacter: ['}', ';']
            },
            renameProvider: {
                prepareProvider: true
            },
            foldingRangeProvider: true,
            selectionRangeProvider: true,
            callHierarchyProvider: true,
            semanticTokensProvider: {
                legend: semanticTokensLegend,
                full: true
            },
            inlayHintProvider: true
        }
    };
});

connection.onInitialized(() => {
    if (hasConfigurationCapability) {
        connection.client.register(DidChangeConfigurationNotification.type, undefined);
    }
    connection.console.log('Pseudocode Language Server initialized');
});

// Configuration handling
connection.onDidChangeConfiguration(change => {
    if (hasConfigurationCapability) {
        documentSettings.clear();
    } else {
        globalSettings = { ...defaultSettings, ...change.settings?.pseudocode };
    }
    documents.all().forEach(validateDocument);
});

function getDocumentSettings(resource: string): Thenable<PseudocodeSettings> {
    if (!hasConfigurationCapability) {
        return Promise.resolve(globalSettings);
    }
    let result = documentSettings.get(resource);
    if (!result) {
        result = connection.workspace.getConfiguration({
            scopeUri: resource,
            section: 'pseudocode'
        });
        documentSettings.set(resource, result);
    }
    return result;
}

// Document change handling
documents.onDidClose(e => {
    documentSettings.delete(e.document.uri);
    analyzer.removeDocument(e.document.uri);
});

documents.onDidChangeContent(change => {
    analyzer.updateDocument(change.document);
    validateDocument(change.document);
});

async function validateDocument(textDocument: TextDocument): Promise<void> {
    const settings = await getDocumentSettings(textDocument.uri);
    if (!settings.diagnosticsEnabled) {
        connection.sendDiagnostics({ uri: textDocument.uri, diagnostics: [] });
        return;
    }

    const diagnostics = analyzer.getDiagnostics(textDocument.uri);
    const limitedDiagnostics = diagnostics.slice(0, settings.maxDiagnostics).map(d => ({
        severity: d.severity === 'error' ? DiagnosticSeverity.Error :
                  d.severity === 'warning' ? DiagnosticSeverity.Warning :
                  DiagnosticSeverity.Information,
        range: d.range,
        message: d.message,
        source: 'pseudocode'
    }));

    connection.sendDiagnostics({ uri: textDocument.uri, diagnostics: limitedDiagnostics });
}

// ============ Completion ============

connection.onCompletion((params: TextDocumentPositionParams): CompletionItem[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const parsed = analyzer.getDocument(params.textDocument.uri);
    const items: CompletionItem[] = [];
    const line = document.getText({
        start: { line: params.position.line, character: 0 },
        end: params.position
    });

    // Check context
    const inString = isInString(line);
    if (inString) return [];

    // Keywords
    for (const keyword of KEYWORDS) {
        items.push({
            label: keyword,
            kind: CompletionItemKind.Keyword,
            detail: 'keyword',
            sortText: '2_' + keyword
        });
    }

    // Built-in functions
    for (const [name, info] of Object.entries(BUILTINS)) {
        items.push({
            label: name,
            kind: CompletionItemKind.Function,
            detail: info.signature,
            documentation: {
                kind: MarkupKind.Markdown,
                value: info.description + (info.example ? `\n\n\`\`\`pseudocode\n${info.example}\n\`\`\`` : '')
            },
            insertText: `${name}($1)`,
            insertTextFormat: 2, // Snippet
            sortText: '1_' + name
        });
    }

    // User-defined functions from parsed document
    if (parsed) {
        for (const [name, fn] of parsed.functions) {
            if (!BUILTINS[name]) {
                const paramStr = fn.params?.join(', ') || '';
                items.push({
                    label: name,
                    kind: CompletionItemKind.Function,
                    detail: `fn ${name}(${paramStr})`,
                    documentation: fn.doc ? { kind: MarkupKind.Markdown, value: fn.doc } : undefined,
                    insertText: `${name}($1)`,
                    insertTextFormat: 2,
                    sortText: '0_' + name
                });
            }
        }

        // Variables in scope
        const currentLine = params.position.line;
        for (const [name, vars] of parsed.variables) {
            const relevantVar = vars.find(v => v.line <= currentLine);
            if (relevantVar) {
                items.push({
                    label: name,
                    kind: CompletionItemKind.Variable,
                    detail: 'variable',
                    sortText: '0_' + name
                });
            }
        }
    }

    return items;
});

connection.onCompletionResolve((item: CompletionItem): CompletionItem => {
    // Add more details if needed
    return item;
});

// ============ Hover ============

connection.onHover((params: TextDocumentPositionParams): Hover | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const word = getWordAtPosition(document, params.position);
    if (!word) return null;

    // Check builtins first
    const builtin = BUILTINS[word];
    if (builtin) {
        let content = `**${builtin.signature}**\n\n${builtin.description}`;
        if (builtin.params) {
            content += '\n\n**Parameters:**\n';
            for (const param of builtin.params) {
                content += `- \`${param.name}\`: ${param.description}\n`;
            }
        }
        if (builtin.returns) {
            content += `\n**Returns:** ${builtin.returns}`;
        }
        if (builtin.example) {
            content += `\n\n**Example:**\n\`\`\`pseudocode\n${builtin.example}\n\`\`\``;
        }
        return {
            contents: { kind: MarkupKind.Markdown, value: content }
        };
    }

    // Check keywords
    if (KEYWORDS.includes(word)) {
        const keywordDocs: Record<string, string> = {
            'fn': 'Define a function.\n\n```pseudocode\nfn name(params)\n    // body\nend\n```',
            'let': 'Declare a variable.\n\n```pseudocode\nlet name = value\n```',
            'if': 'Conditional statement.\n\n```pseudocode\nif condition then\n    // code\nend\n```',
            'for': 'Loop over range or collection.\n\n```pseudocode\nfor i in 1..10 do\n    // code\nend\n```',
            'while': 'Loop while condition is true.\n\n```pseudocode\nwhile condition do\n    // code\nend\n```',
            'match': 'Pattern matching.\n\n```pseudocode\nmatch value\n    case pattern then\n        // code\nend\n```',
            'return': 'Return a value from function.',
            'end': 'End a block (function, if, for, while, match).',
            'true': 'Boolean true value.',
            'false': 'Boolean false value.',
            'nil': 'Null/undefined value.',
            'and': 'Logical AND operator.',
            'or': 'Logical OR operator.',
            'not': 'Logical NOT operator.'
        };
        const doc = keywordDocs[word] || `\`${word}\` keyword`;
        return {
            contents: { kind: MarkupKind.Markdown, value: doc }
        };
    }

    // Check user-defined symbols
    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (parsed) {
        const fn = parsed.functions.get(word);
        if (fn) {
            const paramStr = fn.params?.join(', ') || '';
            let content = `**fn ${word}(${paramStr})**`;
            if (fn.doc) {
                content += `\n\n${fn.doc}`;
            }
            return {
                contents: { kind: MarkupKind.Markdown, value: content }
            };
        }

        const vars = parsed.variables.get(word);
        if (vars && vars.length > 0) {
            return {
                contents: { kind: MarkupKind.Markdown, value: `**${word}** - variable` }
            };
        }
    }

    return null;
});

// ============ Signature Help ============

connection.onSignatureHelp((params: TextDocumentPositionParams): SignatureHelp | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const line = document.getText({
        start: { line: params.position.line, character: 0 },
        end: params.position
    });

    // Find function name and current parameter
    const { functionName, paramIndex } = extractFunctionContext(line);
    if (!functionName) return null;

    // Check builtins
    const builtinParams = BUILTIN_SIGNATURES[functionName];
    if (builtinParams) {
        const paramInfos: ParameterInformation[] = builtinParams.map(p => ({
            label: p,
            documentation: undefined
        }));

        const builtin = BUILTINS[functionName];
        const sig: SignatureInformation = {
            label: `${functionName}(${builtinParams.join(', ')})`,
            documentation: builtin?.description,
            parameters: paramInfos
        };

        return {
            signatures: [sig],
            activeSignature: 0,
            activeParameter: Math.min(paramIndex, builtinParams.length - 1)
        };
    }

    // Check user-defined functions
    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (parsed) {
        const fn = parsed.functions.get(functionName);
        if (fn && fn.params) {
            const paramInfos: ParameterInformation[] = fn.params.map(p => ({
                label: p
            }));

            const sig: SignatureInformation = {
                label: `${functionName}(${fn.params.join(', ')})`,
                documentation: fn.doc,
                parameters: paramInfos
            };

            return {
                signatures: [sig],
                activeSignature: 0,
                activeParameter: Math.min(paramIndex, fn.params.length - 1)
            };
        }
    }

    return null;
});

// ============ Go to Definition ============

connection.onDefinition((params: TextDocumentPositionParams): Definition | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const word = getWordAtPosition(document, params.position);
    if (!word) return null;

    // Search in current document
    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (parsed) {
        // Functions
        const fn = parsed.functions.get(word);
        if (fn) {
            return {
                uri: params.textDocument.uri,
                range: {
                    start: { line: fn.line, character: fn.column },
                    end: { line: fn.line, character: fn.column + word.length }
                }
            };
        }

        // Variables - find first definition
        const vars = parsed.variables.get(word);
        if (vars && vars.length > 0) {
            const firstDef = vars[0];
            return {
                uri: params.textDocument.uri,
                range: {
                    start: { line: firstDef.line, character: firstDef.column },
                    end: { line: firstDef.line, character: firstDef.column + word.length }
                }
            };
        }
    }

    // Search workspace documents
    for (const doc of documents.all()) {
        if (doc.uri === params.textDocument.uri) continue;
        const otherParsed = analyzer.getDocument(doc.uri);
        if (otherParsed) {
            const fn = otherParsed.functions.get(word);
            if (fn) {
                return {
                    uri: doc.uri,
                    range: {
                        start: { line: fn.line, character: fn.column },
                        end: { line: fn.line, character: fn.column + word.length }
                    }
                };
            }
        }
    }

    return null;
});

// ============ References ============

connection.onReferences((params: ReferenceParams): Location[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const word = getWordAtPosition(document, params.position);
    if (!word) return [];

    const locations: Location[] = [];

    // Search all documents
    for (const doc of documents.all()) {
        const parsed = analyzer.getDocument(doc.uri);
        if (!parsed) continue;

        const refs = parsed.references.get(word);
        if (refs) {
            locations.push(...refs);
        }

        // Include definition if requested
        if (params.context.includeDeclaration) {
            const fn = parsed.functions.get(word);
            if (fn) {
                locations.push({
                    uri: doc.uri,
                    range: {
                        start: { line: fn.line, character: fn.column },
                        end: { line: fn.line, character: fn.column + word.length }
                    }
                });
            }
        }
    }

    return locations;
});

// ============ Document Symbols ============

connection.onDocumentSymbol((params): DocumentSymbol[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (!parsed) return [];

    const symbols: DocumentSymbol[] = [];

    // Functions
    for (const [name, fn] of parsed.functions) {
        const endLine = fn.endLine ?? fn.line;
        symbols.push({
            name,
            kind: SymbolKind.Function,
            range: {
                start: { line: fn.line, character: 0 },
                end: { line: endLine, character: 0 }
            },
            selectionRange: {
                start: { line: fn.line, character: fn.column },
                end: { line: fn.line, character: fn.column + name.length }
            },
            detail: fn.params ? `(${fn.params.join(', ')})` : '()',
            children: []
        });
    }

    // Top-level variables
    for (const [name, vars] of parsed.variables) {
        for (const v of vars) {
            if (v.kind === 'variable') {
                symbols.push({
                    name,
                    kind: SymbolKind.Variable,
                    range: {
                        start: { line: v.line, character: 0 },
                        end: { line: v.line, character: 100 }
                    },
                    selectionRange: {
                        start: { line: v.line, character: v.column },
                        end: { line: v.line, character: v.column + name.length }
                    }
                });
            }
        }
    }

    return symbols;
});

// ============ Workspace Symbols ============

connection.onWorkspaceSymbol((params): WorkspaceSymbol[] => {
    const query = params.query.toLowerCase();
    const symbols: WorkspaceSymbol[] = [];

    for (const doc of documents.all()) {
        const parsed = analyzer.getDocument(doc.uri);
        if (!parsed) continue;

        for (const [name, fn] of parsed.functions) {
            if (name.toLowerCase().includes(query)) {
                symbols.push({
                    name,
                    kind: SymbolKind.Function,
                    location: {
                        uri: doc.uri,
                        range: {
                            start: { line: fn.line, character: fn.column },
                            end: { line: fn.line, character: fn.column + name.length }
                        }
                    }
                });
            }
        }
    }

    return symbols;
});

// ============ Document Formatting ============

connection.onDocumentFormatting((params: DocumentFormattingParams): TextEdit[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    return formatDocument(document, params.options.tabSize ?? 4);
});

function formatDocument(document: TextDocument, tabSize: number): TextEdit[] {
    const text = document.getText();
    const lines = text.split('\n');
    const edits: TextEdit[] = [];
    const indent = ' '.repeat(tabSize);
    let currentIndent = 0;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const trimmed = line.trim();

        if (trimmed === '') continue;

        // Decrease indent for closing keywords
        if (/^(end|else|elif|case)\b/.test(trimmed)) {
            currentIndent = Math.max(0, currentIndent - 1);
        }

        const expectedIndent = indent.repeat(currentIndent);
        const actualIndent = line.match(/^\s*/)?.[0] || '';

        if (actualIndent !== expectedIndent && trimmed !== '') {
            edits.push({
                range: {
                    start: { line: i, character: 0 },
                    end: { line: i, character: actualIndent.length }
                },
                newText: expectedIndent
            });
        }

        // Increase indent for opening keywords
        if (/^(fn|if|for|while|match|class|else|elif|case)\b/.test(trimmed) && 
            !trimmed.endsWith('end')) {
            currentIndent++;
        }
    }

    return edits;
}

// ============ Rename ============

connection.onPrepareRename((params: PrepareRenameParams): Range | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const word = getWordAtPosition(document, params.position);
    if (!word) return null;

    // Don't allow renaming keywords or builtins
    if (KEYWORDS.includes(word) || BUILTINS[word]) {
        return null;
    }

    // Check if it's a valid symbol
    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (parsed) {
        if (parsed.functions.has(word) || parsed.variables.has(word)) {
            const wordRange = getWordRangeAtPosition(document, params.position);
            return wordRange;
        }
    }

    return null;
});

connection.onRenameRequest((params: RenameParams): WorkspaceEdit | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const word = getWordAtPosition(document, params.position);
    if (!word) return null;

    const newName = params.newName;
    const changes: { [uri: string]: TextEdit[] } = {};

    // Find all references and replace
    for (const doc of documents.all()) {
        const parsed = analyzer.getDocument(doc.uri);
        if (!parsed) continue;

        const edits: TextEdit[] = [];

        // Check function definition
        const fn = parsed.functions.get(word);
        if (fn) {
            edits.push({
                range: {
                    start: { line: fn.line, character: fn.column },
                    end: { line: fn.line, character: fn.column + word.length }
                },
                newText: newName
            });
        }

        // Check variable definitions
        const vars = parsed.variables.get(word);
        if (vars) {
            for (const v of vars) {
                edits.push({
                    range: {
                        start: { line: v.line, character: v.column },
                        end: { line: v.line, character: v.column + word.length }
                    },
                    newText: newName
                });
            }
        }

        // Check references
        const refs = parsed.references.get(word);
        if (refs) {
            for (const ref of refs) {
                if (ref.uri === doc.uri) {
                    edits.push({
                        range: ref.range,
                        newText: newName
                    });
                }
            }
        }

        if (edits.length > 0) {
            changes[doc.uri] = edits;
        }
    }

    return { changes };
});

// ============ Folding Ranges ============

connection.onFoldingRanges((params): FoldingRange[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const text = document.getText();
    const lines = text.split('\n');
    const ranges: FoldingRange[] = [];
    const stack: { kind: string; line: number }[] = [];

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].trim();

        // Start of block
        if (/^(fn|if|for|while|match|class)\b/.test(line)) {
            stack.push({ kind: line.split(/\s/)[0], line: i });
        }

        // End of block
        if (line === 'end' && stack.length > 0) {
            const start = stack.pop()!;
            ranges.push({
                startLine: start.line,
                endLine: i,
                kind: FoldingRangeKind.Region
            });
        }

        // Comments (consecutive lines)
        if (line.startsWith('//')) {
            let endComment = i;
            while (endComment + 1 < lines.length && lines[endComment + 1].trim().startsWith('//')) {
                endComment++;
            }
            if (endComment > i) {
                ranges.push({
                    startLine: i,
                    endLine: endComment,
                    kind: FoldingRangeKind.Comment
                });
                i = endComment;
            }
        }
    }

    return ranges;
});

// ============ Code Lens ============

connection.onCodeLens((params): CodeLens[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (!parsed) return [];

    const lenses: CodeLens[] = [];

    for (const [name, fn] of parsed.functions) {
        const range = {
            start: { line: fn.line, character: 0 },
            end: { line: fn.line, character: 0 }
        };

        // Run lens
        lenses.push({
            range,
            command: {
                title: '▶ Run',
                command: 'pseudocode.runFunction',
                arguments: [params.textDocument.uri, name]
            }
        });

        // Reference count
        const refs = parsed.references.get(name) || [];
        lenses.push({
            range,
            command: {
                title: `${refs.length} reference${refs.length !== 1 ? 's' : ''}`,
                command: 'editor.action.findReferences',
                arguments: [params.textDocument.uri, { line: fn.line, character: fn.column }]
            }
        });
    }

    return lenses;
});

// ============ Code Actions ============

connection.onCodeAction((params: CodeActionParams): CodeAction[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const actions: CodeAction[] = [];
    const diagnostics = params.context.diagnostics;

    for (const diagnostic of diagnostics) {
        // Suggest fixes for common issues
        if (diagnostic.message.includes('undefined')) {
            const word = document.getText(diagnostic.range);
            
            // Suggest creating a function
            actions.push({
                title: `Create function '${word}'`,
                kind: CodeActionKind.QuickFix,
                diagnostics: [diagnostic],
                edit: {
                    changes: {
                        [params.textDocument.uri]: [{
                            range: {
                                start: { line: document.lineCount, character: 0 },
                                end: { line: document.lineCount, character: 0 }
                            },
                            newText: `\nfn ${word}()\n    // TODO: implement\nend\n`
                        }]
                    }
                }
            });

            // Suggest creating a variable
            actions.push({
                title: `Create variable '${word}'`,
                kind: CodeActionKind.QuickFix,
                diagnostics: [diagnostic],
                edit: {
                    changes: {
                        [params.textDocument.uri]: [{
                            range: {
                                start: { line: diagnostic.range.start.line, character: 0 },
                                end: { line: diagnostic.range.start.line, character: 0 }
                            },
                            newText: `let ${word} = nil\n`
                        }]
                    }
                }
            });
        }
    }

    // Extract function refactoring
    if (params.range.start.line !== params.range.end.line) {
        actions.push({
            title: 'Extract to function',
            kind: CodeActionKind.RefactorExtract,
            command: {
                title: 'Extract to function',
                command: 'pseudocode.extractFunction',
                arguments: [params.textDocument.uri, params.range]
            }
        });
    }

    return actions;
});

// ============ Semantic Tokens ============

connection.languages.semanticTokens.on((params): SemanticTokens => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return { data: [] };

    const builder = new SemanticTokensBuilder();
    const text = document.getText();
    const lines = text.split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];

        // Comments
        const commentMatch = line.match(/\/\/.*/);
        if (commentMatch) {
            builder.push(i, commentMatch.index!, commentMatch[0].length, 6, 0); // comment
        }

        // Keywords
        for (const keyword of KEYWORDS) {
            const regex = new RegExp(`\\b${keyword}\\b`, 'g');
            let match;
            while ((match = regex.exec(line)) !== null) {
                builder.push(i, match.index, keyword.length, 3, 0); // keyword
            }
        }

        // Function definitions
        const fnMatch = line.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
        if (fnMatch) {
            const nameStart = line.indexOf(fnMatch[1]);
            builder.push(i, nameStart, fnMatch[1].length, 0, 1); // function, declaration
        }

        // Function calls
        const callRegex = /([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g;
        let callMatch;
        while ((callMatch = callRegex.exec(line)) !== null) {
            if (!KEYWORDS.includes(callMatch[1])) {
                builder.push(i, callMatch.index, callMatch[1].length, 0, 0); // function
            }
        }

        // Variables
        const letMatch = line.match(/let\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
        if (letMatch) {
            const nameStart = line.indexOf(letMatch[1], line.indexOf('let'));
            builder.push(i, nameStart, letMatch[1].length, 1, 1); // variable, declaration
        }

        // Strings
        const stringRegex = /"[^"]*"|'[^']*'/g;
        let stringMatch;
        while ((stringMatch = stringRegex.exec(line)) !== null) {
            builder.push(i, stringMatch.index, stringMatch[0].length, 4, 0); // string
        }

        // Numbers
        const numberRegex = /\b\d+(\.\d+)?\b/g;
        let numMatch;
        while ((numMatch = numberRegex.exec(line)) !== null) {
            builder.push(i, numMatch.index, numMatch[0].length, 5, 0); // number
        }
    }

    return builder.build();
});

// ============ Inlay Hints ============

connection.languages.inlayHint.on((params): InlayHint[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const hints: InlayHint[] = [];
    const parsed = analyzer.getDocument(params.textDocument.uri);
    const text = document.getText(params.range);
    const lines = text.split('\n');
    const startLine = params.range.start.line;

    // Parameter hints for function calls
    const callRegex = /([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/g;
    
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        let match;
        
        while ((match = callRegex.exec(line)) !== null) {
            const funcName = match[1];
            const argsStr = match[2];
            
            // Skip keywords
            if (KEYWORDS.includes(funcName)) continue;
            
            // Get parameter names
            let paramNames: string[] | undefined = BUILTIN_SIGNATURES[funcName];
            if (!paramNames && parsed) {
                const fn = parsed.functions.get(funcName);
                if (fn && fn.params) paramNames = fn.params;
            }
            
            if (paramNames && argsStr.trim()) {
                const args = splitArgs(argsStr);
                const argsStartPos = match.index + funcName.length + 1;
                let pos = argsStartPos;
                
                for (let j = 0; j < Math.min(args.length, paramNames.length); j++) {
                    const arg = args[j].trim();
                    // Only show hint if arg is not the same as param name
                    if (arg && arg !== paramNames[j] && !paramNames[j].endsWith('?')) {
                        hints.push({
                            position: { line: startLine + i, character: pos },
                            label: `${paramNames[j]}:`,
                            kind: InlayHintKind.Parameter,
                            paddingRight: true
                        });
                    }
                    pos += args[j].length + 1; // +1 for comma
                }
            }
        }
        callRegex.lastIndex = 0;
    }

    return hints;
});

// ============ Document Highlight ============

connection.onDocumentHighlight((params): DocumentHighlight[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const word = getWordAtPosition(document, params.position);
    if (!word) return [];

    const highlights: DocumentHighlight[] = [];
    const text = document.getText();
    const regex = new RegExp(`\\b${escapeRegex(word)}\\b`, 'g');
    let match;

    while ((match = regex.exec(text)) !== null) {
        const pos = document.positionAt(match.index);
        highlights.push({
            range: {
                start: pos,
                end: { line: pos.line, character: pos.character + word.length }
            },
            kind: DocumentHighlightKind.Read
        });
    }

    return highlights;
});

// ============ Call Hierarchy ============

connection.languages.callHierarchy.onPrepare((params): CallHierarchyItem[] | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const word = getWordAtPosition(document, params.position);
    if (!word) return null;

    const parsed = analyzer.getDocument(params.textDocument.uri);
    if (!parsed) return null;

    const fn = parsed.functions.get(word);
    if (!fn) return null;

    return [{
        name: word,
        kind: SymbolKind.Function,
        uri: params.textDocument.uri,
        range: {
            start: { line: fn.line, character: 0 },
            end: { line: fn.endLine ?? fn.line, character: 0 }
        },
        selectionRange: {
            start: { line: fn.line, character: fn.column },
            end: { line: fn.line, character: fn.column + word.length }
        }
    }];
});

connection.languages.callHierarchy.onIncomingCalls((params): CallHierarchyIncomingCall[] => {
    const calls: CallHierarchyIncomingCall[] = [];
    const targetName = params.item.name;

    for (const doc of documents.all()) {
        const parsed = analyzer.getDocument(doc.uri);
        if (!parsed) continue;

        // Find functions that call the target
        for (const [name, fn] of parsed.functions) {
            const refs = parsed.references.get(targetName);
            if (refs) {
                const callsInFunction = refs.filter(ref => 
                    ref.uri === doc.uri &&
                    ref.range.start.line >= fn.line &&
                    ref.range.start.line <= (fn.endLine ?? fn.line)
                );

                if (callsInFunction.length > 0) {
                    calls.push({
                        from: {
                            name,
                            kind: SymbolKind.Function,
                            uri: doc.uri,
                            range: {
                                start: { line: fn.line, character: 0 },
                                end: { line: fn.endLine ?? fn.line, character: 0 }
                            },
                            selectionRange: {
                                start: { line: fn.line, character: fn.column },
                                end: { line: fn.line, character: fn.column + name.length }
                            }
                        },
                        fromRanges: callsInFunction.map(r => r.range)
                    });
                }
            }
        }
    }

    return calls;
});

connection.languages.callHierarchy.onOutgoingCalls((params): CallHierarchyOutgoingCall[] => {
    const document = documents.get(params.item.uri);
    if (!document) return [];

    const parsed = analyzer.getDocument(params.item.uri);
    if (!parsed) return [];

    const fn = parsed.functions.get(params.item.name);
    if (!fn) return [];

    const calls: CallHierarchyOutgoingCall[] = [];
    const startLine = fn.line;
    const endLine = fn.endLine ?? fn.line;

    // Find all function calls within this function
    for (const [name, refs] of parsed.references) {
        const callsInRange = refs.filter(ref =>
            ref.uri === params.item.uri &&
            ref.range.start.line >= startLine &&
            ref.range.start.line <= endLine
        );

        if (callsInRange.length > 0) {
            // Find the function definition
            let targetFn = parsed.functions.get(name);
            let targetUri = params.item.uri;

            if (!targetFn) {
                // Search other documents
                for (const doc of documents.all()) {
                    const otherParsed = analyzer.getDocument(doc.uri);
                    if (otherParsed?.functions.has(name)) {
                        targetFn = otherParsed.functions.get(name);
                        targetUri = doc.uri;
                        break;
                    }
                }
            }

            if (targetFn) {
                calls.push({
                    to: {
                        name,
                        kind: SymbolKind.Function,
                        uri: targetUri,
                        range: {
                            start: { line: targetFn.line, character: 0 },
                            end: { line: targetFn.endLine ?? targetFn.line, character: 0 }
                        },
                        selectionRange: {
                            start: { line: targetFn.line, character: targetFn.column },
                            end: { line: targetFn.line, character: targetFn.column + name.length }
                        }
                    },
                    fromRanges: callsInRange.map(r => r.range)
                });
            }
        }
    }

    return calls;
});

// ============ Selection Range ============

connection.onSelectionRanges((params): SelectionRange[] => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return [];

    const text = document.getText();
    const lines = text.split('\n');

    return params.positions.map(pos => {
        const ranges: Range[] = [];

        // Word level
        const wordRange = getWordRangeAtPosition(document, pos);
        if (wordRange) {
            ranges.push(wordRange);
        }

        // Line level
        ranges.push({
            start: { line: pos.line, character: 0 },
            end: { line: pos.line, character: lines[pos.line]?.length ?? 0 }
        });

        // Block level (find enclosing fn/if/for/while/match)
        const blockRange = findEnclosingBlock(lines, pos.line);
        if (blockRange) {
            ranges.push(blockRange);
        }

        // Document level
        ranges.push({
            start: { line: 0, character: 0 },
            end: { line: lines.length - 1, character: lines[lines.length - 1]?.length ?? 0 }
        });

        // Build nested selection ranges
        let current: SelectionRange | undefined;
        for (let i = ranges.length - 1; i >= 0; i--) {
            current = { range: ranges[i], parent: current };
        }

        return current || { range: ranges[0] };
    });
});

// ============ Helper Functions ============

function escapeRegex(str: string): string {
    return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function getWordAtPosition(document: TextDocument, position: Position): string | null {
    const text = document.getText();
    const offset = document.offsetAt(position);
    
    let start = offset;
    while (start > 0 && /[a-zA-Z0-9_]/.test(text[start - 1])) {
        start--;
    }
    
    let end = offset;
    while (end < text.length && /[a-zA-Z0-9_]/.test(text[end])) {
        end++;
    }
    
    if (start === end) return null;
    return text.substring(start, end);
}

function getWordRangeAtPosition(document: TextDocument, position: Position): Range | null {
    const text = document.getText();
    const offset = document.offsetAt(position);
    
    let start = offset;
    while (start > 0 && /[a-zA-Z0-9_]/.test(text[start - 1])) {
        start--;
    }
    
    let end = offset;
    while (end < text.length && /[a-zA-Z0-9_]/.test(text[end])) {
        end++;
    }
    
    if (start === end) return null;
    
    return {
        start: document.positionAt(start),
        end: document.positionAt(end)
    };
}

function isInString(line: string): boolean {
    let inDouble = false;
    let inSingle = false;
    for (let i = 0; i < line.length; i++) {
        const c = line[i];
        if (c === '"' && !inSingle && (i === 0 || line[i-1] !== '\\')) {
            inDouble = !inDouble;
        } else if (c === "'" && !inDouble && (i === 0 || line[i-1] !== '\\')) {
            inSingle = !inSingle;
        }
    }
    return inDouble || inSingle;
}

function extractFunctionContext(line: string): { functionName: string | null; paramIndex: number } {
    let depth = 0;
    let funcStart = -1;
    let paramIndex = 0;
    
    for (let i = line.length - 1; i >= 0; i--) {
        const c = line[i];
        if (c === ')') depth++;
        else if (c === '(') {
            if (depth === 0) {
                // Find function name
                let end = i;
                while (end > 0 && line[end - 1] === ' ') end--;
                let start = end;
                while (start > 0 && /[a-zA-Z0-9_]/.test(line[start - 1])) start--;
                
                if (start < end) {
                    return {
                        functionName: line.substring(start, end),
                        paramIndex
                    };
                }
            }
            depth--;
        } else if (c === ',' && depth === 0) {
            paramIndex++;
        }
    }
    
    return { functionName: null, paramIndex: 0 };
}

function splitArgs(argsStr: string): string[] {
    const args: string[] = [];
    let current = '';
    let depth = 0;
    
    for (const c of argsStr) {
        if (c === '(' || c === '[' || c === '{') depth++;
        else if (c === ')' || c === ']' || c === '}') depth--;
        else if (c === ',' && depth === 0) {
            args.push(current);
            current = '';
            continue;
        }
        current += c;
    }
    
    if (current) args.push(current);
    return args;
}

function findEnclosingBlock(lines: string[], lineNum: number): Range | null {
    const blockStarts = ['fn', 'if', 'for', 'while', 'match', 'class'];
    let depth = 0;
    let startLine = -1;
    
    // Search backward for block start
    for (let i = lineNum; i >= 0; i--) {
        const trimmed = lines[i].trim();
        if (trimmed === 'end') depth++;
        for (const keyword of blockStarts) {
            if (trimmed.startsWith(keyword + ' ') || trimmed === keyword) {
                if (depth === 0) {
                    startLine = i;
                    break;
                }
                depth--;
            }
        }
        if (startLine >= 0) break;
    }
    
    if (startLine < 0) return null;
    
    // Search forward for matching end
    depth = 1;
    for (let i = startLine + 1; i < lines.length; i++) {
        const trimmed = lines[i].trim();
        for (const keyword of blockStarts) {
            if (trimmed.startsWith(keyword + ' ') || trimmed === keyword) {
                depth++;
            }
        }
        if (trimmed === 'end') {
            depth--;
            if (depth === 0) {
                return {
                    start: { line: startLine, character: 0 },
                    end: { line: i, character: lines[i].length }
                };
            }
        }
    }
    
    return null;
}

// Start the server
documents.listen(connection);
connection.listen();
