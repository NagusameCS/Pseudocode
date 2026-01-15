import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create I/O functions for the runtime.
 */
export declare function createIOFunctions(memory: Memory, stdout: (msg: string) => void, stdin: () => Promise<string>): {
    /**
     * Print a value to stdout.
     */
    print(value: Value): void;
    /**
     * Print a value to stdout with newline.
     */
    println(value: Value): void;
    /**
     * Read a line from stdin.
     */
    input(prompt?: Value): Promise<Value>;
    /**
     * Print formatted output (like printf).
     */
    printf(format: Value, ...args: Value[]): void;
};
//# sourceMappingURL=io.d.ts.map