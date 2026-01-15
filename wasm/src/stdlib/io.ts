/*
 * Pseudocode WASM Standard Library - I/O Functions
 */

import { Value, valString, valNil, isString, asPointer, valToString } from '../runtime/values';
import { Memory } from '../runtime/memory';

/**
 * Create I/O functions for the runtime.
 */
export function createIOFunctions(memory: Memory, stdout: (msg: string) => void, stdin: () => Promise<string>) {
    return {
        /**
         * Print a value to stdout.
         */
        print(value: Value): void {
            const str = valToString(value, memory);
            stdout(str);
        },
        
        /**
         * Print a value to stdout with newline.
         */
        println(value: Value): void {
            const str = valToString(value, memory);
            stdout(str + '\n');
        },
        
        /**
         * Read a line from stdin.
         */
        async input(prompt?: Value): Promise<Value> {
            if (prompt) {
                stdout(valToString(prompt, memory));
            }
            const line = await stdin();
            return memory.allocString(line);
        },
        
        /**
         * Print formatted output (like printf).
         */
        printf(format: Value, ...args: Value[]): void {
            if (!isString(format)) {
                stdout(valToString(format, memory));
                return;
            }
            
            const formatStr = memory.getString(asPointer(format));
            let result = '';
            let argIndex = 0;
            let i = 0;
            
            while (i < formatStr.length) {
                if (formatStr[i] === '%' && i + 1 < formatStr.length) {
                    const spec = formatStr[i + 1];
                    if (spec === '%') {
                        result += '%';
                        i += 2;
                        continue;
                    }
                    
                    if (argIndex < args.length) {
                        const arg = args[argIndex++];
                        switch (spec) {
                            case 's':
                                result += valToString(arg, memory);
                                break;
                            case 'd':
                            case 'i':
                                result += Math.trunc(Number(arg >> 3n)).toString();
                                break;
                            case 'f':
                                result += valToString(arg, memory);
                                break;
                            default:
                                result += valToString(arg, memory);
                        }
                    }
                    i += 2;
                } else {
                    result += formatStr[i];
                    i++;
                }
            }
            
            stdout(result);
        }
    };
}
