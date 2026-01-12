/*
 * Pseudocode Language - Extra Features
 * Inlay Hints, Call Hierarchy, Linked Editing, Outline Enhancements
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';

// ============ Built-in Function Signatures ============

const BUILTIN_SIGNATURES: Record<string, string[]> = {
    // I/O
    'print': ['value', '...args'],
    'input': ['prompt?'],
    'read_file': ['path'],
    'write_file': ['path', 'content'],
    'append_file': ['path', 'content'],

    // Math
    'abs': ['x'],
    'sqrt': ['x'],
    'pow': ['base', 'exponent'],
    'floor': ['x'],
    'ceil': ['x'],
    'round': ['x'],
    'min': ['a', 'b'],
    'max': ['a', 'b'],
    'sin': ['radians'],
    'cos': ['radians'],
    'tan': ['radians'],
    'asin': ['x'],
    'acos': ['x'],
    'atan': ['x'],
    'atan2': ['y', 'x'],
    'log': ['x'],
    'log10': ['x'],
    'log2': ['x'],
    'exp': ['x'],
    'hypot': ['x', 'y'],
    'random': [],
    'randint': ['min', 'max'],

    // String
    'len': ['value'],
    'str': ['value'],
    'upper': ['string'],
    'lower': ['string'],
    'trim': ['string'],
    'split': ['string', 'delimiter'],
    'join': ['array', 'separator'],
    'replace': ['string', 'old', 'new'],
    'contains': ['string', 'substring'],
    'starts_with': ['string', 'prefix'],
    'ends_with': ['string', 'suffix'],
    'substr': ['string', 'start', 'length?'],
    'char_at': ['string', 'index'],
    'index_of': ['string', 'substring'],
    'ord': ['char'],
    'chr': ['code'],

    // Array
    'push': ['array', 'value'],
    'pop': ['array'],
    'shift': ['array'],
    'unshift': ['array', 'value'],
    'slice': ['array', 'start', 'end?'],
    'concat': ['array1', 'array2'],
    'sort': ['array', 'compareFn?'],
    'reverse': ['array'],
    'range': ['start', 'end', 'step?'],
    'map': ['array', 'fn'],
    'filter': ['array', 'predicate'],
    'reduce': ['array', 'fn', 'initial'],
    'find': ['array', 'predicate'],
    'every': ['array', 'predicate'],
    'some': ['array', 'predicate'],
    'flat': ['array', 'depth?'],
    'unique': ['array'],
    'zip': ['array1', 'array2'],

    // Dict
    'dict': [],
    'dict_get': ['dict', 'key', 'default?'],
    'dict_set': ['dict', 'key', 'value'],
    'dict_has': ['dict', 'key'],
    'dict_keys': ['dict'],
    'dict_values': ['dict'],
    'dict_delete': ['dict', 'key'],
    'dict_merge': ['dict1', 'dict2'],
    'keys': ['dict'],
    'values': ['dict'],
    'has': ['dict', 'key'],

    // Type
    'int': ['value'],
    'float': ['value'],
    'type': ['value'],
    'is_nil': ['value'],
    'is_string': ['value'],
    'is_number': ['value'],
    'is_array': ['value'],
    'is_dict': ['value'],
    'is_fn': ['value'],

    // Bitwise
    'bit_and': ['a', 'b'],
    'bit_or': ['a', 'b'],
    'bit_xor': ['a', 'b'],
    'bit_not': ['a'],
    'bit_lshift': ['value', 'shift'],
    'bit_rshift': ['value', 'shift'],
    'popcount': ['value'],
    'clz': ['value'],
    'ctz': ['value'],

    // HTTP
    'http_get': ['url', 'headers?'],
    'http_post': ['url', 'body', 'headers?'],
    'http_put': ['url', 'body', 'headers?'],
    'http_delete': ['url', 'headers?'],

    // JSON
    'json_parse': ['string'],
    'json_stringify': ['value', 'indent?'],

    // System
    'clock': [],
    'sleep': ['milliseconds'],
    'exec': ['command'],
    'env': ['name'],
    'set_env': ['name', 'value'],
    'args': [],
    'exit': ['code?'],

    // Encoding
    'encode_base64': ['data'],
    'decode_base64': ['string'],
    'encode_utf8': ['string'],
    'decode_utf8': ['bytes'],
    'md5': ['data'],
    'sha256': ['data'],
    'hash': ['value'],

    // Vector
    'vec_add': ['a', 'b'],
    'vec_sub': ['a', 'b'],
    'vec_mul': ['a', 'scalar'],
    'vec_div': ['a', 'scalar'],
    'vec_dot': ['a', 'b'],
    'vec_sum': ['vector'],
    'vec_prod': ['vector'],
    'vec_min': ['vector'],
    'vec_max': ['vector'],
    'vec_mean': ['vector'],
};

// Cache for user-defined function signatures with version tracking
interface FunctionCache {
    version: number;
    signatures: Map<string, string[]>;
}
const userFunctionCache = new Map<string, FunctionCache>();

function updateUserFunctions(document: vscode.TextDocument): Map<string, string[]> {
    const uri = document.uri.toString();
    const cached = userFunctionCache.get(uri);
    
    // Return cached if version matches
    if (cached && cached.version === document.version) {
        return cached.signatures;
    }
    
    const signatures = new Map<string, string[]>();
    const text = document.getText();
    
    const fnRegex = /^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/gm;
    let match;

    while ((match = fnRegex.exec(text)) !== null) {
        const name = match[1];
        const params = match[2].split(',').map(p => p.trim()).filter(p => p);
        signatures.set(name, params);
    }
    
    userFunctionCache.set(uri, { version: document.version, signatures });
    return signatures;
}

// ============ Inlay Hints Provider ============

export class PseudocodeInlayHintsProvider implements vscode.InlayHintsProvider {
    provideInlayHints(
        document: vscode.TextDocument,
        range: vscode.Range
    ): vscode.ProviderResult<vscode.InlayHint[]> {
        try {
            const hints: vscode.InlayHint[] = [];
            const text = document.getText(range);
            
            // Skip if range is too small (likely inside a function call being typed)
            if (text.length < 3) {
                return hints;
            }
            
            // Skip if text has unclosed parens (user is still typing)
            const openParens = (text.match(/\(/g) || []).length;
            const closeParens = (text.match(/\)/g) || []).length;
            if (openParens !== closeParens) {
                return hints;
            }
            
            const startOffset = document.offsetAt(range.start);

        // Get cached user function signatures
        const userFunctionSignatures = updateUserFunctions(document);

        // Find function calls
        const callRegex = /([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/g;
        let match;

        while ((match = callRegex.exec(text)) !== null) {
            const fnName = match[1];
            const argsStr = match[2];

            // Skip keywords
            if (['if', 'while', 'for', 'fn', 'match'].includes(fnName)) continue;

            // Get parameter names
            const params = BUILTIN_SIGNATURES[fnName] || userFunctionSignatures.get(fnName);
            if (!params || params.length === 0) continue;

            // Parse arguments
            const args = parseArguments(argsStr);
            if (args.length === 0) continue;

            // Get the position of the opening paren
            const callStart = match.index! + fnName.length;
            const argsStart = text.indexOf('(', callStart) + 1;

            // Add hints for each argument
            let argOffset = 0;
            for (let i = 0; i < args.length && i < params.length; i++) {
                const arg = args[i];
                const paramName = params[i].replace('?', '');

                // Skip if argument looks like it already has label (e.g., "key: value")
                if (arg.includes(':')) continue;

                // Skip very short arguments or numbers
                if (arg.trim().length <= 2 && !isNaN(Number(arg.trim()))) {
                    // Still add hint for numbers to clarify meaning
                }

                const hintPos = document.positionAt(startOffset + argsStart + argOffset);

                hints.push({
                    position: hintPos,
                    label: `${paramName}:`,
                    kind: vscode.InlayHintKind.Parameter,
                    paddingRight: true
                });

                // Move past this argument (including comma and space)
                argOffset += arg.length;
                if (i < args.length - 1) {
                    const nextComma = argsStr.indexOf(',', argOffset);
                    if (nextComma !== -1) {
                        argOffset = nextComma + 1;
                        // Skip whitespace
                        while (argOffset < argsStr.length && argsStr[argOffset] === ' ') {
                            argOffset++;
                        }
                    }
                }
            }
        }

        return hints;
        } catch (error) {
            console.error('Inlay hints error:', error);
            return [];
        }
    }
}

function parseArguments(argsStr: string): string[] {
    const args: string[] = [];
    let current = '';
    let depth = 0;
    let inString = false;
    let stringChar = '';

    for (let i = 0; i < argsStr.length; i++) {
        const char = argsStr[i];

        if (!inString) {
            if (char === '"' || char === "'") {
                inString = true;
                stringChar = char;
            } else if (char === '(' || char === '[' || char === '{') {
                depth++;
            } else if (char === ')' || char === ']' || char === '}') {
                depth--;
            } else if (char === ',' && depth === 0) {
                args.push(current.trim());
                current = '';
                continue;
            }
        } else if (char === stringChar && argsStr[i - 1] !== '\\') {
            inString = false;
        }

        current += char;
    }

    if (current.trim()) {
        args.push(current.trim());
    }

    return args;
}

// ============ Call Hierarchy Provider ============

// Helper to escape regex special characters
function escapeRegex(str: string): string {
    return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

export class PseudocodeCallHierarchyProvider implements vscode.CallHierarchyProvider {
    prepareCallHierarchy(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.ProviderResult<vscode.CallHierarchyItem | vscode.CallHierarchyItem[]> {
        try {
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) return undefined;

            const word = document.getText(wordRange);
            const escapedWord = escapeRegex(word);
            const line = document.lineAt(position.line);

            // Check if it's a function definition
            const fnMatch = line.text.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
            if (fnMatch && fnMatch[1] === word) {
                return new vscode.CallHierarchyItem(
                    vscode.SymbolKind.Function,
                    word,
                    '',
                    document.uri,
                    line.range,
                    wordRange
                );
            }

            // Check if it's a function call
            const callMatch = line.text.match(new RegExp(`\\b${escapedWord}\\s*\\(`));
            if (callMatch) {
                // Find the function definition
                const text = document.getText();
                const defMatch = text.match(new RegExp(`^fn\\s+${escapedWord}\\s*\\(`, 'm'));
                if (defMatch) {
                    const defPos = document.positionAt(defMatch.index!);
                    const defLine = document.lineAt(defPos.line);
                    return new vscode.CallHierarchyItem(
                        vscode.SymbolKind.Function,
                        word,
                        '',
                        document.uri,
                        defLine.range,
                        new vscode.Range(defPos, defPos.translate(0, word.length))
                    );
                }
            }

            return undefined;
        } catch (error) {
            console.error('Call hierarchy prepare error:', error);
            return undefined;
        }
    }

    provideCallHierarchyIncomingCalls(
        item: vscode.CallHierarchyItem
    ): vscode.ProviderResult<vscode.CallHierarchyIncomingCall[]> {
        return this.findCalls(item, 'incoming');
    }

    provideCallHierarchyOutgoingCalls(
        item: vscode.CallHierarchyItem
    ): vscode.ProviderResult<vscode.CallHierarchyOutgoingCall[]> {
        return this.findCalls(item, 'outgoing');
    }

    private async findCalls(
        item: vscode.CallHierarchyItem,
        direction: 'incoming' | 'outgoing'
    ): Promise<any[]> {
        try {
            const document = await vscode.workspace.openTextDocument(item.uri);
            const text = document.getText();
            const calls: any[] = [];
            const escapedName = escapeRegex(item.name);

            if (direction === 'incoming') {
                // Find all places where this function is called
                const callRegex = new RegExp(`\\b${escapedName}\\s*\\(`, 'g');
                let match;

                while ((match = callRegex.exec(text)) !== null) {
                    const pos = document.positionAt(match.index);
                    const line = document.lineAt(pos.line);

                    // Find the enclosing function
                    let enclosingFn = 'global';
                    for (let i = pos.line; i >= 0; i--) {
                        const fnMatch = document.lineAt(i).text.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
                        if (fnMatch) {
                            enclosingFn = fnMatch[1];
                            break;
                        }
                    }

                    if (enclosingFn !== item.name) {
                        const callerItem = new vscode.CallHierarchyItem(
                            vscode.SymbolKind.Function,
                            enclosingFn,
                            '',
                            document.uri,
                            line.range,
                            new vscode.Range(pos, pos.translate(0, item.name.length))
                        );

                        calls.push({
                            from: callerItem,
                            fromRanges: [new vscode.Range(pos, pos.translate(0, item.name.length))]
                        });
                    }
                }
            } else {
                // Find all function calls within this function
                const fnRegex = new RegExp(`^fn\\s+${escapedName}\\s*\\([^)]*\\)`, 'm');
                const fnMatch = text.match(fnRegex);
                if (!fnMatch) return [];

                const fnStart = fnMatch.index!;
                const fnStartPos = document.positionAt(fnStart);

                // Find the end of this function
                let depth = 1;
                let fnEnd = fnStart + fnMatch[0].length;
                const lines = text.substring(fnEnd).split('\n');

                for (const line of lines) {
                    fnEnd += line.length + 1;
                    if (line.trim().match(/^(fn|if|while|for|match)\b.*\b(then|do)?\s*$/)) {
                        depth++;
                    } else if (line.trim() === 'end') {
                        depth--;
                        if (depth === 0) break;
                    }
                }

                const fnBody = text.substring(fnStart, fnEnd);
                const callRegex = /([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g;
                let match;

                while ((match = callRegex.exec(fnBody)) !== null) {
                    const calledFn = match[1];
                    if (['if', 'while', 'for', 'fn', 'match'].includes(calledFn)) continue;
                    if (calledFn === item.name) continue; // Skip self

                    const callPos = document.positionAt(fnStart + match.index);
                    const callLine = document.lineAt(callPos.line);

                    const calleeItem = new vscode.CallHierarchyItem(
                        vscode.SymbolKind.Function,
                        calledFn,
                        BUILTIN_SIGNATURES[calledFn] ? '(builtin)' : '',
                        document.uri,
                        callLine.range,
                        new vscode.Range(callPos, callPos.translate(0, calledFn.length))
                    );

                    calls.push({
                        to: calleeItem,
                        fromRanges: [new vscode.Range(callPos, callPos.translate(0, calledFn.length))]
                    });
                }
            }

            return calls;
        } catch (error) {
            console.error('Call hierarchy findCalls error:', error);
            return [];
        }
    }
}

// ============ Linked Editing Ranges (for paired keywords) ============

export class PseudocodeLinkedEditingRangeProvider implements vscode.LinkedEditingRangeProvider {
    provideLinkedEditingRanges(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.ProviderResult<vscode.LinkedEditingRanges> {
        const line = document.lineAt(position.line);
        const word = document.getText(document.getWordRangeAtPosition(position));

        // Only handle block starters and 'end'
        const blockKeywords = ['fn', 'if', 'while', 'for', 'match'];

        if (!blockKeywords.includes(word) && word !== 'end') {
            return undefined;
        }

        // This is a simplified implementation
        // Real implementation would match fn..end, if..end pairs
        return undefined;
    }
}

// ============ Color Provider (for color literals) ============

export class PseudocodeColorProvider implements vscode.DocumentColorProvider {
    provideDocumentColors(document: vscode.TextDocument): vscode.ProviderResult<vscode.ColorInformation[]> {
        try {
            const colors: vscode.ColorInformation[] = [];
            const text = document.getText();

            // Match hex colors like "#ff0000" or "#f00"
            const hexRegex = /"#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})"/g;
            let match;

            while ((match = hexRegex.exec(text)) !== null) {
                const hex = match[1];
                const pos = document.positionAt(match.index);
                const endPos = document.positionAt(match.index + match[0].length);
                const range = new vscode.Range(pos, endPos);

                let r, g, b;
                if (hex.length === 3) {
                    r = parseInt(hex[0] + hex[0], 16) / 255;
                    g = parseInt(hex[1] + hex[1], 16) / 255;
                    b = parseInt(hex[2] + hex[2], 16) / 255;
                } else {
                    r = parseInt(hex.substring(0, 2), 16) / 255;
                    g = parseInt(hex.substring(2, 4), 16) / 255;
                    b = parseInt(hex.substring(4, 6), 16) / 255;
                }

                colors.push(new vscode.ColorInformation(range, new vscode.Color(r, g, b, 1)));
            }

            // Match rgb(r, g, b) patterns
            const rgbRegex = /rgb\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)/g;
            while ((match = rgbRegex.exec(text)) !== null) {
                const r = parseInt(match[1]) / 255;
                const g = parseInt(match[2]) / 255;
                const b = parseInt(match[3]) / 255;

                const pos = document.positionAt(match.index);
                const endPos = document.positionAt(match.index + match[0].length);
                const range = new vscode.Range(pos, endPos);

                colors.push(new vscode.ColorInformation(range, new vscode.Color(r, g, b, 1)));
            }

            return colors;
        } catch (error) {
            console.error('Color provider error:', error);
            return [];
        }
    }

    provideColorPresentations(
        color: vscode.Color,
        context: { document: vscode.TextDocument; range: vscode.Range }
    ): vscode.ProviderResult<vscode.ColorPresentation[]> {
        try {
            const r = Math.round(color.red * 255);
            const g = Math.round(color.green * 255);
            const b = Math.round(color.blue * 255);

            const hex = `"#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}"`;
            const rgb = `rgb(${r}, ${g}, ${b})`;

            return [
                new vscode.ColorPresentation(hex),
                new vscode.ColorPresentation(rgb)
            ];
        } catch (error) {
            console.error('Color presentation error:', error);
            return [];
        }
    }
}

// ============ Selection Range Provider ============

export class PseudocodeSelectionRangeProvider implements vscode.SelectionRangeProvider {
    provideSelectionRanges(
        document: vscode.TextDocument,
        positions: vscode.Position[]
    ): vscode.ProviderResult<vscode.SelectionRange[]> {
        try {
            return positions.map(position => this.getSelectionRange(document, position));
        } catch (error) {
            console.error('Selection range error:', error);
            return [];
        }
    }

    private getSelectionRange(document: vscode.TextDocument, position: vscode.Position): vscode.SelectionRange {
        const line = document.lineAt(position.line);
        const text = document.getText();
        const ranges: vscode.Range[] = [];

        // Word range
        const wordRange = document.getWordRangeAtPosition(position);
        if (wordRange) {
            ranges.push(wordRange);
        }

        // Line range (without leading whitespace)
        const lineTextStart = line.firstNonWhitespaceCharacterIndex;
        ranges.push(new vscode.Range(
            position.line, lineTextStart,
            position.line, line.text.length
        ));

        // Full line range
        ranges.push(line.range);

        // Find enclosing block
        let blockStart = position.line;
        let blockEnd = position.line;
        let depth = 0;

        // Search backward for block start
        for (let i = position.line; i >= 0; i--) {
            const lineText = document.lineAt(i).text.trim();
            if (lineText === 'end') {
                depth++;
            } else if (lineText.match(/^(fn|if|while|for|match)\b/)) {
                if (depth === 0) {
                    blockStart = i;
                    break;
                }
                depth--;
            }
        }

        // Search forward for block end
        depth = 1;
        for (let i = blockStart + 1; i < document.lineCount; i++) {
            const lineText = document.lineAt(i).text.trim();
            if (lineText.match(/^(fn|if|while|for|match)\b.*\b(then|do)?\s*$/)) {
                depth++;
            } else if (lineText === 'end') {
                depth--;
                if (depth === 0) {
                    blockEnd = i;
                    break;
                }
            }
        }

        if (blockStart !== position.line || blockEnd !== position.line) {
            ranges.push(new vscode.Range(blockStart, 0, blockEnd, document.lineAt(blockEnd).text.length));
        }

        // Document range
        ranges.push(new vscode.Range(0, 0, document.lineCount - 1, document.lineAt(document.lineCount - 1).text.length));

        // Build chain from smallest to largest
        let current: vscode.SelectionRange | undefined;
        for (let i = ranges.length - 1; i >= 0; i--) {
            current = new vscode.SelectionRange(ranges[i], current);
        }

        return current || new vscode.SelectionRange(line.range);
    }
}

// ============ Type Definition Provider ============

export class PseudocodeTypeDefinitionProvider implements vscode.TypeDefinitionProvider {
    provideTypeDefinition(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.ProviderResult<vscode.Definition> {
        try {
            // In Pseudocode, types are inferred, so we redirect to definition
            const wordRange = document.getWordRangeAtPosition(position);
            if (!wordRange) return undefined;

            const word = document.getText(wordRange);
            const escapedWord = escapeRegex(word);
            const text = document.getText();

            // Look for function definition
            const fnMatch = text.match(new RegExp(`^fn\\s+${escapedWord}\\s*\\(`, 'm'));
            if (fnMatch) {
                const pos = document.positionAt(fnMatch.index! + 3); // Skip "fn "
                return new vscode.Location(document.uri, pos);
            }

            // Look for variable declaration
            const letMatch = text.match(new RegExp(`let\\s+${escapedWord}\\s*=`, 'm'));
            if (letMatch) {
                const pos = document.positionAt(letMatch.index! + 4); // Skip "let "
                return new vscode.Location(document.uri, pos);
            }

            return undefined;
        } catch (error) {
            console.error('Type definition error:', error);
            return undefined;
        }
    }
}

// ============ Register Extra Features ============

export function registerExtraFeatures(context: vscode.ExtensionContext): void {
    const config = vscode.workspace.getConfiguration('pseudocode');
    const selector: vscode.DocumentSelector = { language: 'pseudocode', scheme: 'file' };

    // Inlay Hints
    if (config.get<boolean>('inlayHints.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerInlayHintsProvider(selector, new PseudocodeInlayHintsProvider())
        );
    }

    // Call Hierarchy
    if (config.get<boolean>('callHierarchy.enabled', false)) {
        context.subscriptions.push(
            vscode.languages.registerCallHierarchyProvider(selector, new PseudocodeCallHierarchyProvider())
        );
    }

    // Color Provider - always enabled, lightweight
    context.subscriptions.push(
        vscode.languages.registerColorProvider(selector, new PseudocodeColorProvider())
    );

    // Selection Ranges - always enabled, only runs on explicit action
    context.subscriptions.push(
        vscode.languages.registerSelectionRangeProvider(selector, new PseudocodeSelectionRangeProvider())
    );

    // Type Definition - always enabled, only runs on explicit action
    context.subscriptions.push(
        vscode.languages.registerTypeDefinitionProvider(selector, new PseudocodeTypeDefinitionProvider())
    );

    console.log('Pseudocode extra features registered');
}
