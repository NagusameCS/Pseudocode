/*
 * Pseudocode Language Analyzer
 * 
 * Parses and analyzes Pseudocode source files to extract symbols,
 * references, and diagnostics.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import { TextDocument } from 'vscode-languageserver-textdocument';
import { Location, Range, DiagnosticSeverity } from 'vscode-languageserver/node';
import { BUILTINS, KEYWORDS } from './builtins';

export interface SymbolInfo {
    name: string;
    kind: 'function' | 'variable' | 'parameter' | 'class' | 'enum';
    line: number;
    column: number;
    endLine?: number;
    params?: string[];
    doc?: string;
}

export interface DiagnosticInfo {
    range: Range;
    message: string;
    severity: 'error' | 'warning' | 'info';
}

export interface ParsedDocument {
    uri: string;
    version: number;
    functions: Map<string, SymbolInfo>;
    variables: Map<string, SymbolInfo[]>;
    references: Map<string, Location[]>;
    classes: Map<string, SymbolInfo>;
    enums: Map<string, SymbolInfo>;
    diagnostics: DiagnosticInfo[];
}

interface ScopeStack {
    type: 'function' | 'if' | 'for' | 'while' | 'match' | 'class';
    name?: string;
    startLine: number;
    variables: Set<string>;
}

export class PseudocodeAnalyzer {
    private documents: Map<string, ParsedDocument> = new Map();

    updateDocument(document: TextDocument): ParsedDocument {
        const uri = document.uri;
        const cached = this.documents.get(uri);
        
        // Skip if already parsed at this version
        if (cached && cached.version === document.version) {
            return cached;
        }

        const parsed = this.parseDocument(document);
        this.documents.set(uri, parsed);
        return parsed;
    }

    getDocument(uri: string): ParsedDocument | undefined {
        return this.documents.get(uri);
    }

    removeDocument(uri: string): void {
        this.documents.delete(uri);
    }

    getDiagnostics(uri: string): DiagnosticInfo[] {
        const parsed = this.documents.get(uri);
        return parsed?.diagnostics ?? [];
    }

    private parseDocument(document: TextDocument): ParsedDocument {
        const text = document.getText();
        const lines = text.split('\n');
        
        const result: ParsedDocument = {
            uri: document.uri,
            version: document.version,
            functions: new Map(),
            variables: new Map(),
            references: new Map(),
            classes: new Map(),
            enums: new Map(),
            diagnostics: []
        };

        const scopeStack: ScopeStack[] = [];
        let pendingDoc = '';

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            const trimmed = line.trim();

            // Skip empty lines
            if (!trimmed) {
                pendingDoc = '';
                continue;
            }

            // Capture doc comments
            if (trimmed.startsWith('//')) {
                pendingDoc = trimmed.substring(2).trim();
                continue;
            }

            // Function definitions
            const fnMatch = trimmed.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/);
            if (fnMatch) {
                const name = fnMatch[1];
                const paramsStr = fnMatch[2];
                const params = paramsStr
                    .split(',')
                    .map(p => p.trim())
                    .filter(p => p);
                
                const column = line.indexOf(name);
                
                const fnInfo: SymbolInfo = {
                    name,
                    kind: 'function',
                    line: i,
                    column,
                    params,
                    doc: pendingDoc || undefined
                };

                // Check for duplicate function
                if (result.functions.has(name)) {
                    result.diagnostics.push({
                        range: { 
                            start: { line: i, character: column },
                            end: { line: i, character: column + name.length }
                        },
                        message: `Function '${name}' is already defined`,
                        severity: 'error'
                    });
                }

                result.functions.set(name, fnInfo);

                // Add function scope
                scopeStack.push({
                    type: 'function',
                    name,
                    startLine: i,
                    variables: new Set(params)
                });

                // Add parameters as variables
                for (const param of params) {
                    const paramName = param.split(':')[0].trim();
                    const paramCol = line.indexOf(paramName, column);
                    
                    if (!result.variables.has(paramName)) {
                        result.variables.set(paramName, []);
                    }
                    result.variables.get(paramName)!.push({
                        name: paramName,
                        kind: 'parameter',
                        line: i,
                        column: paramCol >= 0 ? paramCol : column
                    });
                }

                pendingDoc = '';
                continue;
            }

            // Class definitions
            const classMatch = trimmed.match(/^class\s+([a-zA-Z_][a-zA-Z0-9_]*)(?:\s+extends\s+([a-zA-Z_][a-zA-Z0-9_]*))?/);
            if (classMatch) {
                const name = classMatch[1];
                const column = line.indexOf(name);
                
                result.classes.set(name, {
                    name,
                    kind: 'class',
                    line: i,
                    column,
                    doc: pendingDoc || undefined
                });

                scopeStack.push({
                    type: 'class',
                    name,
                    startLine: i,
                    variables: new Set()
                });

                pendingDoc = '';
                continue;
            }

            // Enum definitions
            const enumMatch = trimmed.match(/^enum\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+?)\s*end$/);
            if (enumMatch) {
                const name = enumMatch[1];
                const column = line.indexOf(name);
                
                result.enums.set(name, {
                    name,
                    kind: 'enum',
                    line: i,
                    column,
                    doc: pendingDoc || undefined
                });

                pendingDoc = '';
                continue;
            }

            // Variable declarations (let)
            const letMatches = trimmed.matchAll(/let\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*=/g);
            for (const match of letMatches) {
                const name = match[1];
                const column = line.indexOf(name);

                if (!result.variables.has(name)) {
                    result.variables.set(name, []);
                }
                
                result.variables.get(name)!.push({
                    name,
                    kind: 'variable',
                    line: i,
                    column
                });

                // Add to current scope
                if (scopeStack.length > 0) {
                    scopeStack[scopeStack.length - 1].variables.add(name);
                }
            }

            // Control flow blocks
            if (/^if\b/.test(trimmed)) {
                scopeStack.push({ type: 'if', startLine: i, variables: new Set() });
            } else if (/^for\b/.test(trimmed)) {
                scopeStack.push({ type: 'for', startLine: i, variables: new Set() });
                
                // Extract loop variable
                const forMatch = trimmed.match(/^for\s+([a-zA-Z_][a-zA-Z0-9_]*)\s+in\b/);
                if (forMatch) {
                    const loopVar = forMatch[1];
                    const col = line.indexOf(loopVar);
                    
                    if (!result.variables.has(loopVar)) {
                        result.variables.set(loopVar, []);
                    }
                    result.variables.get(loopVar)!.push({
                        name: loopVar,
                        kind: 'variable',
                        line: i,
                        column: col
                    });
                    scopeStack[scopeStack.length - 1].variables.add(loopVar);
                }
            } else if (/^while\b/.test(trimmed)) {
                scopeStack.push({ type: 'while', startLine: i, variables: new Set() });
            } else if (/^match\b/.test(trimmed)) {
                scopeStack.push({ type: 'match', startLine: i, variables: new Set() });
            }

            // End of block
            if (trimmed === 'end') {
                const scope = scopeStack.pop();
                if (scope) {
                    // Update function end line
                    if (scope.type === 'function' && scope.name) {
                        const fn = result.functions.get(scope.name);
                        if (fn) {
                            fn.endLine = i;
                        }
                    } else if (scope.type === 'class' && scope.name) {
                        const cls = result.classes.get(scope.name);
                        if (cls) {
                            cls.endLine = i;
                        }
                    }
                } else {
                    // Unmatched end
                    result.diagnostics.push({
                        range: {
                            start: { line: i, character: 0 },
                            end: { line: i, character: 3 }
                        },
                        message: "Unexpected 'end' - no matching block",
                        severity: 'error'
                    });
                }
            }

            // Function calls and identifier references
            this.extractReferences(line, i, document.uri, result, scopeStack);

            pendingDoc = '';
        }

        // Check for unclosed blocks
        for (const scope of scopeStack) {
            result.diagnostics.push({
                range: {
                    start: { line: scope.startLine, character: 0 },
                    end: { line: scope.startLine, character: 10 }
                },
                message: `Unclosed ${scope.type} block - missing 'end'`,
                severity: 'error'
            });
        }

        return result;
    }

    private extractReferences(
        line: string,
        lineNum: number,
        uri: string,
        result: ParsedDocument,
        scopeStack: ScopeStack[]
    ): void {
        // Skip comments
        const commentIdx = line.indexOf('//');
        const codePart = commentIdx >= 0 ? line.substring(0, commentIdx) : line;

        // Function calls
        const callRegex = /([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g;
        let match;

        while ((match = callRegex.exec(codePart)) !== null) {
            const name = match[1];
            
            // Skip keywords
            if (KEYWORDS.includes(name)) continue;

            if (!result.references.has(name)) {
                result.references.set(name, []);
            }
            
            result.references.get(name)!.push({
                uri,
                range: {
                    start: { line: lineNum, character: match.index },
                    end: { line: lineNum, character: match.index + name.length }
                }
            });

            // Check for undefined function (not builtin, not defined in file)
            if (!BUILTINS[name] && !result.functions.has(name)) {
                // Might be defined later in file, skip for now
                // Or it might be from another file
            }
        }

        // Variable references (identifiers not followed by parentheses)
        const identRegex = /\b([a-zA-Z_][a-zA-Z0-9_]*)\b(?!\s*\()/g;
        
        while ((match = identRegex.exec(codePart)) !== null) {
            const name = match[1];
            
            // Skip keywords and builtins
            if (KEYWORDS.includes(name) || BUILTINS[name]) continue;
            
            // Skip string contents (rough check)
            const beforeMatch = codePart.substring(0, match.index);
            const quotes = (beforeMatch.match(/"/g) || []).length + (beforeMatch.match(/'/g) || []).length;
            if (quotes % 2 !== 0) continue;

            if (!result.references.has(name)) {
                result.references.set(name, []);
            }
            
            result.references.get(name)!.push({
                uri,
                range: {
                    start: { line: lineNum, character: match.index },
                    end: { line: lineNum, character: match.index + name.length }
                }
            });
        }
    }
}
