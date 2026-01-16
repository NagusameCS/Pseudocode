"use strict";
/*
 * Pseudocode WASM Standard Library - I/O Functions
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.createIOFunctions = createIOFunctions;
const values_1 = require("../runtime/values");
/**
 * Create I/O functions for the runtime.
 */
function createIOFunctions(memory, stdout, stdin) {
    return {
        /**
         * Print a value to stdout.
         */
        print(value) {
            const str = (0, values_1.valToString)(value, memory);
            stdout(str);
        },
        /**
         * Print a value to stdout with newline.
         */
        println(value) {
            const str = (0, values_1.valToString)(value, memory);
            stdout(str + '\n');
        },
        /**
         * Read a line from stdin.
         */
        async input(prompt) {
            if (prompt) {
                stdout((0, values_1.valToString)(prompt, memory));
            }
            const line = await stdin();
            return memory.allocString(line);
        },
        /**
         * Print formatted output (like printf).
         */
        printf(format, ...args) {
            if (!(0, values_1.isString)(format)) {
                stdout((0, values_1.valToString)(format, memory));
                return;
            }
            const formatStr = memory.getString((0, values_1.asPointer)(format));
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
                                result += (0, values_1.valToString)(arg, memory);
                                break;
                            case 'd':
                            case 'i':
                                result += Math.trunc(Number(arg >> 3n)).toString();
                                break;
                            case 'f':
                                result += (0, values_1.valToString)(arg, memory);
                                break;
                            default:
                                result += (0, values_1.valToString)(arg, memory);
                        }
                    }
                    i += 2;
                }
                else {
                    result += formatStr[i];
                    i++;
                }
            }
            stdout(result);
        }
    };
}
//# sourceMappingURL=io.js.map